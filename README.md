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

# Answers
## 2.3.1 Make yourself familiar with the mouse interface. How does the mouse guarantee thread-safe functionality?

Via Mutex is the thread-safe functionality guaranteed. The relatve codes are already showed in the Lectures_RTOS.pdf.

the structure "mouse" plays a role like an agency. It obtains the newest location of the mouse from SDL_Event, lock the values with a Mutex and any other function/task should only get the location of mouse from the agency, unter the rule of mutex. 

Now that the API from SDL_Event is without a lock and therefore may cause compitition over the resources, an agency with a lock could be created to ensure the thread-safe functionality.

## 3.1 What is the kernel tick? What is a tickless kernel?

In terms of functionality is the kernel tick a measure of time. In terms of implement is the kernel tick some kind of periodically triggered interrupt, in which certain actions are to execute, such as scheduling of tasks and the tick hook function.

Obviously, the frequently triggered interrupt "tick" requires plenty of resources and energy, and as a result is not worthwhile when no tasks but the idle task is running. Therefore, a tickless kernel could be used. In this mode, "the FreeRTOS tickless idle mode stops the periodic tick interrupt during idle periods (periods when there are no application tasks that are able to execute), then makes a correcting adjustment to the RTOS tick count value when the tick interrupt is restarted."(https://www.freertos.org/low-power-tickless-rtos.html) So the system is able to run in a low-power state.

## 3.2.2.5 Experiment with the task stack size. What happens if it is too low?

Interesting, because it seems the task, whose stack is statically allocated, doesn’t use any of the stack space, even if I have defined some local variables.

If I allocate the minimum(here as configMINIMAL_STACK_SIZE, and it is equal to 4) at the moment I create the task, than the water mark during the running will be printed as 4. Even if I allocate 0, the printed water mark would also become 0, but without any visible disturb to the correct result.

So I guess, maybe FreeROTS has some measures, which can automatically prevent a certain degree of damage from a stackoverflow?

## 3.3.3 Scheduling Insights

The output would be perfectly consistent with the priority, as long as the tasks have different priority.

If the tasks have the same priority, then the order should be **almost consistent with the order the tasks are resumed**, which is 1, 2 then 4. Note that 3 should be waked by 2, so 3 is the last one to resume. But they have the same priority and they all call delay function, so the sequence might change more and more probably along with the programm running because of some random possibility.

# Some notes

## Screens
For each sub-exercises in Exercises 3 I created one screen of solution.

## 3.3.2
I think it would not be reasonable to restrict every task within one tick. Even though we can use such as a combination of interrupts and timer to insure a task be scheduled once every tick, we cannot force but 'pray' that the task runs through within one tick. To 'restrict' a task within one tick, it seems be more relevant with improvement on hardware and counting on a light requirement.

# Others

Something may help in comprehension if a thorough check of codes is expected.


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

 
 
