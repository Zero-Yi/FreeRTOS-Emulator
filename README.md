# Buttons
## exercise 2
`A, B, C, D` press to make the corresponding counter increase
`left mouse button` click to reset the counters

## exercise 3.2.1
`E` press to switch between the solutions

## exercise 3.2.3
`1, 2` press to make the corresponding counter increase

## exercise 3.2.4
`P` press to pause the counting down

# Log

## 6. Dec
 Arriving exercise 3.3

### sync event group
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
 |State five|Task3output|BIT_14|

 ### two dimension linked list
 |Mainnode tick1|Mainnode tick2|...|Mainnode tick15|
  |:----:|:----:|:----:| :----: |
 |Subnode task31|Subnode task32|...|Subnode task31|
 ||Subnode task33|...||
 |...|...|...|...|

 when 15 tick have finished, then Task3output starts to print out, by walking through the linked list to get the information.

 ## 7. Dec

 ### Change in 3.2.2
 In my implementation I choose to use the task notification instead of a binary semaphore, because I need to send a value as well as the signal.

 ### myEventGroup
 this event group is to tell the corresponding tasks that the screen has been updated, so that they can submit new drawing jobs. It works like a multi-task version of "DrawSignal".
 |State|Task to notify|Bit|
 | :---: | :---: | :---: |
 |State two|Task211|BIT_0|
 |State two|Task211|BIT_1|
 |State three|Task22aux|BIT_2|
 |State four|Task23aux|BIT_4|
 |State five|Task3output|BIT_5|

 Besides, this event group is also used by Task31-34 to notify Task3output that they have finished the print (to the 2-D linked list) and it is time to output the data on the screen. So after initiating the 2-D linked list, the Task3output will wait for the other 4 tasks by using
 `xEventGroupWaitBits(myEventGroup, BIT_8|BIT_9|BIT_11, pdTRUE, pdTRUE, portMAX_DELAY)`
|Task|Bit|
|:---:|:---:|
|Task31|BIT_8|
|Task32|BIT_9|
|Task34|BIT_11|

Note that Task33 isn't in the chart, because it can be controlled by the Task32.

 
 