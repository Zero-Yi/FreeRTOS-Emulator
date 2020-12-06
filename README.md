# Log

## 6. Dec
 Arriving exercise 3.3

 Brockdown become more and more freuquent, try to use xEventGroupSync() to make a sync point before the state changing, so that all the lock would be sure to get returned and all the tasks get into the next state orderly.

 |State| Task | Sync Bit |
 |:----:|:----:|:----:|
 ||BufferSwapTask|BIT_0|
 ||StateMachine|BIT_1|
 |State one|Task1|BIT_2|
 |State two|Task211|BIT_3|
 |State two|Task211|BIT_4|
 |State three|Task221|BIT_5|
 |State three|Task222|BIT_6|
 |State three|Task22aux|BIT_7|
 |State four|Task23|BIT_8|
 |State four|Task23aux|BIT_9|

 |Task1|BIT10|