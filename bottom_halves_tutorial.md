# Linux Bottom Halves Deep Dive

## Audience and goals
This tutorial is for experienced kernel developers who already know interrupts, spinlocks, preemption, and common driver architecture. The focus is the *why* and *how* behind Linux bottom halves: **softirqs, tasklets, and workqueues**, with practical guidance on choosing among them and implementing driver flows correctly.

---

## 1) Problem statement: why bottom halves exist
A hard IRQ handler (top half) runs in a highly constrained context:
- local interrupts are masked on that CPU while the handler runs,
- it cannot sleep,
- latency matters for the whole system.

So the top half should do only urgent device-ack and minimal bookkeeping, then defer substantial work to a bottom-half mechanism that matches the work's constraints.

Historically Linux had the old "BH" mechanism, then softirqs/tasklets, and later workqueues grew into the sleepable deferred-execution workhorse.

---

## 2) Execution-context taxonomy (the part that prevents bugs)

### 2.1 Hard IRQ context (top half)
- Entry from interrupt/exception path.
- Cannot sleep.
- Must use `GFP_ATOMIC` for emergency allocations.
- Should be short and bounded.

### 2.2 SoftIRQ context
- Runs in interrupt context (not process context).
- Cannot sleep.
- May run:
  - on return from hard IRQ (`irq_exit()` path),
  - in `ksoftirqd/N` kernel thread fallback when load is high / budgets exceeded.
- Per-CPU pending bitmap; same softirq type can run concurrently on multiple CPUs.

### 2.3 Tasklet context
- Built on top of softirq (`TASKLET_SOFTIRQ` / `HI_SOFTIRQ`).
- Cannot sleep.
- Serialization rule: *same tasklet instance* never runs concurrently on multiple CPUs.
- Different tasklets can run in parallel.
- Widely considered legacy/de-emphasized in modern kernel development (prefer workqueues or dedicated threaded designs).

### 2.4 Workqueue context
- Runs in process context via worker kthreads.
- Can sleep, block, and use normal locking primitives that may sleep.
- Supports concurrency control, NUMA/cpu affinity hints, delayed work, high-priority, unbound pools, and memory-reclaim-safe execution.

---

## 3) SoftIRQ design and implementation internals

### 3.1 Core data model
Softirqs are statically defined vectors (enum index into per-CPU pending bits). Typical types include `NET_RX_SOFTIRQ`, `NET_TX_SOFTIRQ`, `TIMER_SOFTIRQ`, `RCU_SOFTIRQ`, etc.

Core pieces:
- `open_softirq(nr, action)` registers the handler.
- `raise_softirq(nr)` marks pending.
- `__do_softirq()` dispatch loop executes pending handlers with restart budget limits.

### 3.2 Dispatch semantics and fairness
`__do_softirq()` loops over pending bits and invokes registered handlers. To avoid softirq monopolizing CPU, loop restarts are budgeted (`MAX_SOFTIRQ_RESTART` style behavior). If not fully drained quickly, remaining work is punted to `ksoftirqd/N`.

Consequence: under sustained load, work that "feels interrupt-like" may effectively execute in thread context (`ksoftirqd`) but *still under softirq constraints* (must not sleep).

### 3.3 Preemption and RT considerations
On PREEMPT_RT, semantics differ because many traditionally non-threaded paths become threaded/preemptible. Never encode fragile assumptions like "softirq always runs with hard interrupt latency profile"; design for the abstract context guarantees (sleepable vs non-sleepable, locking rules), not incidental scheduling behavior.

### 3.4 Locking patterns in softirq handlers
- Use spinlocks with `_bh` variants when coordinating with process context that may also touch shared state (`spin_lock_bh()` / `spin_unlock_bh()`).
- Keep critical sections small.
- Avoid lock inversion with hardirq locks.

---

## 4) Tasklets: architecture, pros/cons, current stance

Tasklets wrap softirq with per-tasklet serialization:
- scheduled via `tasklet_schedule()` / `tasklet_hi_schedule()`,
- executed from tasklet softirq handler,
- safe against self-reentrancy of same tasklet instance.

Pros:
- easy single-instance serialization.

Cons:
- still atomic context (no sleeping),
- less explicit scalability/concurrency control than workqueues,
- subsystem trend is away from tasklets in new code.

Practical advice: only use when atomic context is truly required and conversion cost/risk is unjustified. For new driver code, prefer workqueues or threaded IRQ + workqueue depending on latency and sleep needs.

---

## 5) Workqueue deep dive

### 5.1 Why workqueues dominate modern deferred work
Workqueues give process context while preserving kernel-managed pooling and backpressure. They avoid spawning ad-hoc kthreads per device and centralize scheduling/accounting.

### 5.2 Concepts
- **work item**: `struct work_struct` callback.
- **delayed work**: timer-backed deferred callback (`struct delayed_work`).
- **worker pool**: backing kthreads.
- **bound vs unbound**:
  - bound: tends to keep CPU locality,
  - unbound: favors throughput and flexible placement.
- **ordered workqueue**: strict in-order execution (effectively max-active=1).

### 5.3 API highlights
- `alloc_workqueue(name, flags, max_active, ...)`
- `INIT_WORK()` / `INIT_DELAYED_WORK()`
- `queue_work()` / `queue_delayed_work()`
- cancellation/teardown: `cancel_work_sync()`, `cancel_delayed_work_sync()`, `flush_workqueue()`, `destroy_workqueue()`.

### 5.4 Flag selection heuristics
- `WQ_UNBOUND`: CPU intensive or long blocking tasks that should not pin one CPU.
- `WQ_HIGHPRI`: latency-sensitive tasks.
- `WQ_MEM_RECLAIM`: needed if work may participate in memory reclaim paths; guarantees rescuer semantics.
- `WQ_FREEZABLE`: cooperate with suspend/freezer.

### 5.5 Lifetime and teardown correctness
Most production bugs are here:
- Ensure object lifetime exceeds any queued callback.
- During remove/unload:
  1. stop producers (IRQ/NAPI/timers),
  2. cancel/flush queued work synchronously,
  3. free resources only after callbacks can no longer run.

---

## 6) How to choose (decision matrix)

1. **Must sleep (I2C/SPI transfer with sleep, mutex, firmware load, blocking alloc)?**
   - Use **workqueue**.
2. **Must run very soon and cannot sleep; high packet/timer style throughput?**
   - Use **softirq** (usually only in core subsystems, not random drivers).
3. **Need atomic deferred callback with simple serialization of one instance?**
   - Tasklet is possible but usually reconsider architecture (often workqueue is cleaner).
4. **Need threaded handling with device IRQ semantics?**
   - Consider **threaded IRQ** (`request_threaded_irq`) possibly with additional workqueue stages.

For most new drivers: **top half does minimal ack + queue_work()**.

---

## 7) Driver-oriented example (recommended pattern)

Below is a compact, realistic skeleton for a PCI-like device where IRQ top half only captures status and queues work.

```c
struct foo_dev {
    void __iomem *mmio;
    int irq;

    spinlock_t lock;              /* protects fast-path state in irq */
    struct workqueue_struct *wq;
    struct work_struct irq_work;

    u32 pending_status;
    bool shutting_down;
};

static irqreturn_t foo_irq_handler(int irq, void *data)
{
    struct foo_dev *f = data;
    u32 st;
    unsigned long flags;

    st = readl(f->mmio + FOO_INT_STATUS);
    if (!st)
        return IRQ_NONE;

    /* Ack device quickly to quench interrupt storm. */
    writel(st, f->mmio + FOO_INT_ACK);

    spin_lock_irqsave(&f->lock, flags);
    f->pending_status |= st;
    spin_unlock_irqrestore(&f->lock, flags);

    /* defer heavy/sleepable work */
    queue_work(f->wq, &f->irq_work);
    return IRQ_HANDLED;
}

static void foo_irq_workfn(struct work_struct *work)
{
    struct foo_dev *f = container_of(work, struct foo_dev, irq_work);
    unsigned long flags;
    u32 st;

    spin_lock_irqsave(&f->lock, flags);
    st = f->pending_status;
    f->pending_status = 0;
    spin_unlock_irqrestore(&f->lock, flags);

    if (!st)
        return;

    if (st & FOO_INT_RX)
        foo_handle_rx_path(f); /* may sleep */

    if (st & FOO_INT_TX)
        foo_complete_tx(f);    /* may sleep */

    if (st & FOO_INT_ERR)
        foo_recover(f);        /* may sleep, maybe reset device */
}

static int foo_probe(...)
{
    ...
    spin_lock_init(&f->lock);
    INIT_WORK(&f->irq_work, foo_irq_workfn);

    f->wq = alloc_workqueue("foo_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
    if (!f->wq)
        return -ENOMEM;

    ret = request_irq(f->irq, foo_irq_handler, IRQF_SHARED, "foo", f);
    if (ret)
        goto err_destroy_wq;

    ...
    return 0;

err_destroy_wq:
    destroy_workqueue(f->wq);
    return ret;
}

static void foo_remove(...)
{
    ...
    free_irq(f->irq, f);

    /* Ensure no queued callbacks are still running. */
    cancel_work_sync(&f->irq_work);
    destroy_workqueue(f->wq);
    ...
}
```

### Why this pattern works
- IRQ handler is bounded and deterministic.
- Sleepable operations are moved out of atomic context.
- Teardown uses `cancel_work_sync()` to close use-after-free races.

### Common variations
- If RX path is high-throughput networking, integrate **NAPI** instead of generic workqueue.
- If latency-critical tiny follow-up is atomic-only, use per-CPU data + softirq-like patterns in core stacks.
- For periodic polling fallback, pair with delayed work.

---

## 8) Typical bugs and anti-patterns

1. **Sleeping in softirq/tasklet**
   - e.g., taking mutex, blocking I/O, `msleep()`.
2. **Using `GFP_KERNEL` in atomic context**
   - use preallocation/mempools or `GFP_ATOMIC` when unavoidable.
3. **Forgetting teardown synchronization**
   - freeing device state before `cancel_work_sync()`/`flush_workqueue()`.
4. **Excessive work in hard IRQ**
   - causes interrupt latency spikes.
5. **Incorrect lock pairing across contexts**
   - process context vs softirq/hardirq mismatch.

---

## 9) Observability and debugging toolbox

- `/proc/softirqs`: per-CPU softirq counters.
- ftrace / tracepoints:
  - `irq:*`, `softirq:*`, `workqueue:*` events.
- lockdep: catches locking misuse across irq/bh/process contexts.
- `CONFIG_DEBUG_OBJECTS_WORK`: detects workqueue lifecycle misuse.
- `perf sched` and trace-cmd: latency and scheduling analysis.

Debug strategy:
1. Validate context assumptions (`might_sleep()` splats are your friend).
2. Instrument enqueue/dequeue/execute timestamps.
3. Stress remove/unload paths concurrently with interrupts.

---

## 10) Modern guidance summary
- Prefer **workqueues** for most driver deferred work.
- Keep hard IRQ handlers tiny.
- Treat **tasklets** as legacy unless there's strong reason.
- Use **softirqs** mostly in subsystem/core code requiring high-performance atomic deferred execution.
- Design teardown first; it defines correctness under stress.

If you want, next step I can generate:
1. a **real, buildable mini kernel module** (with Kconfig/Makefile) implementing this pattern,
2. a **trace-based lab** script to observe hardirq→softirq/workqueue transitions,
3. or a **PREEMPT_RT-specific variant** of the design and locking rules.
