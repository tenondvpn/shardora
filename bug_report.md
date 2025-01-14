### 1. follower 给 leader 同步交易是一次性的，如果 leader 没有接收到，则这个交易就会丢失。
比如：s3_4 通过 vote msg 将 gid: 123 同步给 leader，leader 由于该 vote msg 验证失败没有接收消息（比如已经收到足够的 vote），则该交易丢失。这个问题会在 leader 轮换时发生。
解决思路：
● 交易同步和 hotstuff 消息验证的逻辑应该解耦，至少验证逻辑应当解耦
● follower 同步给 leader 的交易应该有个待确认状态

### 2. leader 可能会通过 Vote msg 将自己的 txs 传递给自己，会被 GidValid 拒绝，但对应交易也会从自己的交易池中删除
这个问题会在解决了 1 之后发生
