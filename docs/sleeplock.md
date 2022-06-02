# 睡眠锁

## 实现

睡眠锁的实现，核心是 `sleep` 和 `wakeup` 两个函数。

`sleep` 函数给进程设置等待资源 `chan`，将进程状态设为 `SLEEPING`。然后保存内核现场，并执行 `yield`。处于 `SLEEPING` 状态的进程无法被调度。获取睡眠锁时，如果发现睡眠锁被锁住，则需要执行 `sleep` 函数，等待睡眠锁的 `chan`。

`wakeup` 函数遍历所有进程控制块，将所有等待资源 `chan` 的唤醒。当释放睡眠锁时，执行 `wakeup` 函数，将所有因该睡眠锁而睡眠的进程的状态改为 `RUNNABLE`。

由于睡眠锁本身也是一种临界资源，因此在多核场景下，对睡眠锁的操作需要使用自旋锁。

## 应用

睡眠锁主要有两个应用。一个是在块缓存中，另一个是在 `dirent` 中。