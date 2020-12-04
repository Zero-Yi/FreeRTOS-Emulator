#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

#include <SDL2/SDL_scancode.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Font.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_FreeRTOS_Utils.h"
#include "TUM_Print.h"

#include "AsyncIO.h"

#define mainGENERIC_PRIORITY (tskIDLE_PRIORITY)
#define mainGENERIC_STACK_SIZE ((unsigned short)2560)

// state machine betroffen
#define STATE_QUEUE_LENGTH 1
#define STATE_COUNT 2
#define STATE_ONE 0
#define STATE_TWO 1
#define NEXT_TASK 0
#define PREV_TASK 1
#define STARTING_STATE STATE_ONE
#define STATE_DEBOUNCE_DELAY 300

#define PI 3.1415926
// rad per ms
#define ROTATION_SPEED PI / 2000
#define ROTATION_RADIUS 40

#define TRI_HEIGHT 20
#define TRI_BOTTOM 20
#define TRI_PEEK_X SCREEN_WIDTH / 2
#define TRI_PEEK_Y SCREEN_HEIGHT / 2 - TRI_HEIGHT / 2 + 50

// the initial center point 
#define CIR_INITIAL_X (SCREEN_WIDTH / 2 - ROTATION_RADIUS)
#define CIR_INITIAL_Y SCREEN_HEIGHT / 2 + 50
#define CIR_RADIUS 10

// the initial center point 
#define SQU_INITIAL_X (SCREEN_WIDTH / 2 + ROTATION_RADIUS)
#define SQU_INITIAL_Y SCREEN_HEIGHT / 2 + 50
#define SQU_LENGTH 20

//by pixel
#define MOVING_TEXT_RANGE 200
//pixel per ms
#define MOVING_TEXT_SPEED 0.1

#define FPS (TickType_t)50

#define KEYCODE(CHAR) SDL_SCANCODE_##CHAR
#define PRINT_TASK_ERROR(task) PRINT_ERROR("Failed to print task ##task");

#define MOVE_WITH_MOUSE 1

#define A_COUNTER_POSITION 0
#define B_COUNTER_POSITION 1
#define C_COUNTER_POSITION 2
#define D_COUNTER_POSITION 3
#define NUMBER_OF_TRACED_BUTTONS 4

// period in ms
#define PERIOD_TASK21 1000
#define PERIOD_TASK22 500

static TaskHandle_t StateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t Task1 = NULL;
static TaskHandle_t Task21 = NULL;
static TaskHandle_t Task22 = NULL;

static QueueHandle_t StateQueue = NULL;
static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;
static TimerHandle_t Task21Timer = NULL;
static TimerHandle_t Task22Timer = NULL;

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

void *const pvTimerID = 0;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

typedef struct ABCDcounter {
    short counter[4]; 
    SemaphoreHandle_t lock;
} ABCDcounter_t;

static buttons_buffer_t buttons = { 0 };
static ABCDcounter_t myABCDcounter = { 0 };

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
}

void checkDraw(unsigned char status, const char *msg)
{
    if (status) {
        if (msg)
            fprints(stderr, "[ERROR] %s, %s\n", msg,
                    tumGetErrorMessage());
        else {
            fprints(stderr, "[ERROR] %s\n", tumGetErrorMessage());
        }
    }
}

void vSwapBuffers(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();
    const TickType_t frameratePeriod = configTICK_RATE_HZ / FPS;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumDrawUpdateScreen();
            tumEventFetchEvents(FETCH_EVENT_BLOCK);
            checkDraw(tumDrawClear(White), __FUNCTION__);
            xSemaphoreGive(ScreenLock);
            xSemaphoreGive(DrawSignal);
            vTaskDelayUntil(&xLastWakeTime,
                            pdMS_TO_TICKS(frameratePeriod));
        }
    }
}

void changeState(volatile unsigned char *state, unsigned char forwards)
{
    switch (forwards) {
        case NEXT_TASK:
            if (*state == STATE_COUNT - 1) {
                *state = 0;
            }
            else {
                (*state)++;
            }
            break;
        case PREV_TASK:
            if (*state == 0) {
                *state = STATE_COUNT - 1;
            }
            else {
                (*state)--;
            }
            break;
        default:
            break;
    }
}

static int vCheckStateInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        if (buttons.buttons[KEYCODE(E)]) {
            buttons.buttons[KEYCODE(E)] = 0;
            if (StateQueue) {
                xSemaphoreGive(buttons.lock);
                xQueueSend(StateQueue, &next_state_signal, 0);
                // prints("E pressed\n");
                return 0;
            }
            xSemaphoreGive(buttons.lock);
            return -1;
        }
        xSemaphoreGive(buttons.lock);
    }
    return 0;
}

/*
 * Example basic state machine with sequential states
 */
void basicSequentialStateMachine(void *pvParameters)
{
    unsigned char current_state = STARTING_STATE; // Default state
    unsigned char state_changed =
        1; // Only re-evaluate state if it has changed
    unsigned char input = 0;

    const int state_change_period = STATE_DEBOUNCE_DELAY;

    TickType_t last_change = xTaskGetTickCount();

    while (1) {
        if (state_changed) {
            goto initial_state;
        }

        // Handle state machine input
        if (StateQueue)
            if (xQueueReceive(StateQueue, &input, portMAX_DELAY) ==
                pdTRUE)
                if (xTaskGetTickCount() - last_change >
                    state_change_period) {
                    changeState(&current_state, input);
                    state_changed = 1;
                    last_change = xTaskGetTickCount();
                }

initial_state:
        // Handle current state
        if (state_changed) {
            switch (current_state) {
                case STATE_ONE:
                        vTaskSuspend(Task21);
                        vTaskSuspend(Task22);
                        vTaskResume(Task1);
                        xTimerStop(Task21Timer, portMAX_DELAY);
                        xTimerStop(Task22Timer, portMAX_DELAY);
                        prints("Change to state 1\n");
                    break;
                case STATE_TWO:
                        vTaskSuspend(Task1);
                        vTaskResume(Task21);
                        vTaskResume(Task22);
                        xTimerStart(Task21Timer, portMAX_DELAY);
                        xTimerStart(Task22Timer, portMAX_DELAY);
                        prints("Change to state 2\n");
                    break;
                default:
                    break;
            }
            state_changed = 0;
        }
    }
}

void moveWithMouse(signed short *x, signed short *y){
    if ( MOVE_WITH_MOUSE ){
        *x += (tumEventGetMouseX() - SCREEN_WIDTH / 2) / 2;
        *y += (tumEventGetMouseY() - SCREEN_HEIGHT / 2) / 2;
    }
}

void deMoveWithMouse(signed short *x, signed short *y){
    if ( MOVE_WITH_MOUSE ){
        *x -= (tumEventGetMouseX() - SCREEN_WIDTH / 2) / 2;
        *y -= (tumEventGetMouseY() - SCREEN_HEIGHT / 2) / 2;
    }
}

#ifdef MOVE_WITH_MOUSE
#define MOVE_WITH_MOUSE_X(XCOORD) XCOORD + (tumEventGetMouseX() - SCREEN_WIDTH / 2) / 2
#define MOVE_WITH_MOUSE_Y(YCOORD) YCOORD + (tumEventGetMouseY() - SCREEN_HEIGHT / 2) / 2
#else
#define MOVE_WITH_MOUSE_X(XCOORD) XCOORD 
#define MOVE_WITH_MOUSE_Y(YCOORD) YCOORD 
#endif

void drawTheTriangle(void)
{
    coord_t peek = {MOVE_WITH_MOUSE_X(TRI_PEEK_X), MOVE_WITH_MOUSE_Y(TRI_PEEK_Y)};
    coord_t bottomPointleft = {MOVE_WITH_MOUSE_X(TRI_PEEK_X - TRI_BOTTOM / 2), 
                                MOVE_WITH_MOUSE_Y(TRI_PEEK_Y + TRI_HEIGHT)};
    coord_t bottomPointright = {MOVE_WITH_MOUSE_X(TRI_PEEK_X + TRI_BOTTOM / 2), 
                                MOVE_WITH_MOUSE_Y(TRI_PEEK_Y + TRI_HEIGHT)};
    coord_t points[3] = {peek, bottomPointleft, bottomPointright};
                                    
    checkDraw(tumDrawTriangle(points, TUMBlue),
             __FUNCTION__);
}

void drawTheCircle(TickType_t initialWakeTime)
{   
    TickType_t xLastWakeTime = xTaskGetTickCount();
    double daurationSinceBeginning = pdMS_TO_TICKS(xLastWakeTime - initialWakeTime);
    double radSinceBeginning = daurationSinceBeginning * ROTATION_SPEED;
    
    signed short x = CIR_INITIAL_X + 
                    (ROTATION_RADIUS - ROTATION_RADIUS * cos(radSinceBeginning));
    signed short y = CIR_INITIAL_Y -
                    ROTATION_RADIUS * sin(radSinceBeginning);

    moveWithMouse(&x, &y);
    checkDraw(tumDrawCircle(x, y, CIR_RADIUS, Red),
             __FUNCTION__);
}

void drawTheSquare(TickType_t initialWakeTime)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    double daurationSinceBeginning = pdMS_TO_TICKS(xLastWakeTime - initialWakeTime);
    double radSinceBeginning = daurationSinceBeginning * ROTATION_SPEED;

    signed short x = SQU_INITIAL_X - SQU_LENGTH / 2 -
                    (ROTATION_RADIUS - ROTATION_RADIUS * cos(radSinceBeginning));
    signed short y = SQU_INITIAL_Y - SQU_LENGTH / 2 +
                    ROTATION_RADIUS * sin(radSinceBeginning);

    moveWithMouse(&x, &y);
    checkDraw(tumDrawFilledBox(x, y, SQU_LENGTH, SQU_LENGTH, Green),
             __FUNCTION__);
}

void drawTheFixedText(void)
{
    static char str[100] = { 0 };
    static int text_width;
    ssize_t prev_font_size = tumFontGetCurFontSize();
    ssize_t current_font_size = (ssize_t)30;

    tumFontSetSize(current_font_size);

    sprintf(str, "I'm not supposed to move!");

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str, 
                              MOVE_WITH_MOUSE_X(SCREEN_WIDTH / 2 - text_width / 2),
                              MOVE_WITH_MOUSE_Y(SCREEN_HEIGHT - current_font_size * 2), 
                              Black),
                  __FUNCTION__);

    tumFontSetSize(prev_font_size);
}

void drawTheMovingText(TickType_t prevWakeTime)
{
    static char str[100] = { 0 };
    static int text_width;

    static signed short x = SCREEN_WIDTH / 2 - MOVING_TEXT_RANGE;
    static signed short y = DEFAULT_FONT_SIZE * 2;
    static char direction = 'r';

    TickType_t xLastWakeTime = xTaskGetTickCount();
    signed short increment_on_time = 1000 * (xLastWakeTime - prevWakeTime) / (float)configTICK_RATE_HZ;
    // prints("movment:%hd\n", (signed short)(MOVING_TEXT_SPEED * increment_on_time));
    
    if (direction == 'r')
        x += (signed short)(MOVING_TEXT_SPEED * increment_on_time);
    else if (direction == 'l')
        x -= (signed short)(MOVING_TEXT_SPEED * increment_on_time);
    else
        prints("Undefined direction!");

    sprintf(str, "I'm supposed to move!");

    moveWithMouse(&x, &y);
    if (!tumGetTextSize((char *)str, &text_width, NULL)){
        checkDraw(tumDrawText(str, x, y, Black),
                  __FUNCTION__);
        if (x >= SCREEN_WIDTH / 2 + MOVING_TEXT_RANGE - text_width)
            direction = 'l';
        else if (x <= SCREEN_WIDTH / 2 - MOVING_TEXT_RANGE)
            direction = 'r';
    }
    deMoveWithMouse(&x, &y);
}

#define BEING_PRESSED 1
#define NOT_BEING_PRESSED 0
#define DEBOUNCE_DELAY_BY_TICK (TickType_t)10

void countTheABCDs(void)
{
    // from lastFlipTime[0] to lastFlipTime[3] for ABCD
    static TickType_t lastFlipTime[NUMBER_OF_TRACED_BUTTONS] = { 0 };
    TickType_t currentTime = xTaskGetTickCount();

    // from left to right stands for ABCD
    static short lastButtonsState[NUMBER_OF_TRACED_BUTTONS] = { 0 };
    static short buttonsState[NUMBER_OF_TRACED_BUTTONS] = { 0 };
    short readingButtonsState[NUMBER_OF_TRACED_BUTTONS] = { 0 };

    if (tumEventGetMouseLeft()) {
        // click the left mouse button to reset the counter
        if(xSemaphoreTake(myABCDcounter.lock, portMAX_DELAY) == pdTRUE){
            memset(myABCDcounter.counter, 0, sizeof(short) * NUMBER_OF_TRACED_BUTTONS);
            xSemaphoreGive(myABCDcounter.lock);
        }
    }

    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        readingButtonsState[A_COUNTER_POSITION] = buttons.buttons[KEYCODE(A)];
        readingButtonsState[B_COUNTER_POSITION] = buttons.buttons[KEYCODE(B)];
        readingButtonsState[C_COUNTER_POSITION] = buttons.buttons[KEYCODE(C)];
        readingButtonsState[D_COUNTER_POSITION] = buttons.buttons[KEYCODE(D)];
        xSemaphoreGive(buttons.lock);
    }

    // check the button one by one
    for (int index = 0; index < NUMBER_OF_TRACED_BUTTONS; index++) {
        if (readingButtonsState[index] != lastButtonsState[index])
            // means the corresponding button state changes
            lastFlipTime[index] = currentTime;
    }

    for (int index = 0; index < NUMBER_OF_TRACED_BUTTONS; index++) { 
        // check one by one
        if (currentTime - lastFlipTime[index] > DEBOUNCE_DELAY_BY_TICK){ 
            // state stay in being-changed long enough
            if (readingButtonsState[index] != buttonsState[index]){
                // compared with the last confirmed state
                buttonsState[index] = readingButtonsState[index];
                // confirm this change
                if (buttonsState[index] == BEING_PRESSED)
                    // if this change is from not pressed to pressed
                    if(xSemaphoreTake(myABCDcounter.lock, portMAX_DELAY) == pdTRUE){
                        myABCDcounter.counter[index]++;
                        xSemaphoreGive(myABCDcounter.lock);
                    }
                    
            }
        }
    }
    memcpy(lastButtonsState, readingButtonsState, sizeof(short) * NUMBER_OF_TRACED_BUTTONS);
}

void drawButtonCounts(void)
{
    static char str[100] = { 0 };
    static int text_width;

    sprintf(str, "Axis 1: %5d | Axis 2: %5d", tumEventGetMouseX(),
            tumEventGetMouseY());

    if (!tumGetTextSize((char *)str, &text_width, NULL))
        checkDraw(tumDrawText(str, 
                              MOVE_WITH_MOUSE_X(SCREEN_WIDTH / 2 - text_width / 2),
                              MOVE_WITH_MOUSE_Y(DEFAULT_FONT_SIZE * 5),
                              Skyblue), __FUNCTION__);

    if (xSemaphoreTake(myABCDcounter.lock, 0) == pdTRUE) {
        sprintf(str, "A: %d | B: %d | C: %d | D: %d",
                myABCDcounter.counter[A_COUNTER_POSITION],
                myABCDcounter.counter[B_COUNTER_POSITION],
                myABCDcounter.counter[C_COUNTER_POSITION],
                myABCDcounter.counter[D_COUNTER_POSITION]);
        xSemaphoreGive(myABCDcounter.lock);
       if (!tumGetTextSize((char *)str, &text_width, NULL))
           checkDraw(tumDrawText(str, 
                                 MOVE_WITH_MOUSE_X(SCREEN_WIDTH / 2 - text_width / 2), 
                                 MOVE_WITH_MOUSE_Y(DEFAULT_FONT_SIZE * 7), 
                                 Skyblue), __FUNCTION__);
    }
}

// this task is do the exercise2
void vTask1(void *pvParameters)
{
    TickType_t lastWakeTime, prevWakeTime, initialWakeTime;
    initialWakeTime = xTaskGetTickCount();
    lastWakeTime = initialWakeTime;
    prevWakeTime = lastWakeTime;

    while (1) {
        if (DrawSignal)
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                lastWakeTime = xTaskGetTickCount();                

                xGetButtonInput(); // Update global input
                countTheABCDs(); // Update the counter

                // Clear screen
                // checkDraw(tumDrawClear(White), __FUNCTION__);

                // Draw the fixed elements
                drawTheTriangle();
                drawTheFixedText();
                drawButtonCounts();

                // Draw the dynamic elements
                drawTheCircle(initialWakeTime);
                drawTheSquare(initialWakeTime);
                drawTheMovingText(prevWakeTime);
                drawButtonCounts();

                vCheckStateInput();

                prevWakeTime = lastWakeTime;
            }
    }
}

void vTask21TimerCallback( TimerHandle_t xTimer )
{
    xTaskNotifyGive(Task21);
    // prints("task21 timer expires!\n");
}

void vTask22TimerCallback( TimerHandle_t xTimer )
{
    xTaskNotifyGive(Task22);
    // prints("task22 timer expires!\n");
}

#define Task21Circle_X SCREEN_WIDTH / 4
#define Task21Circle_Y SCREEN_HEIGHT / 2
#define Task22Circle_X SCREEN_WIDTH / 4 * 3
#define Task22Circle_Y SCREEN_HEIGHT / 2
#define Task2Circle_Radius 20

short flagToggle(unsigned short *flag)
{
    if ( *flag > 1)
        return -1;
    else if( *flag == 1 )
        *flag = 0;
    else // means *flag = 0
        *flag = 1;
    return 0;
}

void vTask21(void *pvParameters)
{
    static unsigned short flag = 0;
    u_int32_t notificationValue = 0;

    while (1) {
        if (DrawSignal){
            notificationValue = ulTaskNotifyTake(pdTRUE, 0);
            // prints("notificationValue: %d\n", notificationValue);
            if ( notificationValue > 0 ) // check the toggle singal
            {
                if( flagToggle(&flag) == -1)
                    prints("Error! Task21 toggle flag not 1 or 0\n");
                prints("state toggled! now: %hd\n", flag);
            }

            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update agency

                if (flag == 1){ // on drawing phase
                    checkDraw(tumDrawCircle(Task21Circle_X, Task21Circle_Y, 
                                        Task2Circle_Radius, Orange), __FUNCTION__);
                    prints("Draw!\n");
                }

                vCheckStateInput();
            }
        }
    }
}

void vTask22(void *pvParameters)
{
    static unsigned short flag = 0;

    while (1) {
        if (DrawSignal)
            if ( ulTaskNotifyTake(pdTRUE, 0) > 0 ) // check the toggle singal
                if( flagToggle(&flag) == -1)
                    prints("Error! Task22 toggle flag not 1 or 0\n");

            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                xGetButtonInput(); // Update agency
                
                if (flag == 1) // on drawing phase
                    checkDraw(tumDrawCircle(Task22Circle_X, Task22Circle_Y, 
                                        Task2Circle_Radius, Orange), __FUNCTION__);
                                    
                vCheckStateInput();
            }
    }
}

int main(int argc, char *argv[])
{
    char *bin_folder_path = tumUtilGetBinFolderPath(argv[0]);

    printf("Initializing: ");

    if (tumDrawInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize drawing");
        goto err_init_drawing;
    }

    if (tumEventInit()) {
        PRINT_ERROR("Failed to initialize events");
        goto err_init_events;
    }

    if (tumSoundInit(bin_folder_path)) {
        PRINT_ERROR("Failed to initialize audio");
        goto err_init_audio;
    }

    buttons.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!buttons.lock) {
        PRINT_ERROR("Failed to create buttons lock");
        goto err_buttons_lock;
    }

    myABCDcounter.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!myABCDcounter.lock) {
        PRINT_ERROR("Failed to create myABCDcounter lock");
        goto err_myABCDcounter_lock;
    }

    DrawSignal = xSemaphoreCreateBinary(); // Screen buffer locking
    if (!DrawSignal) {
        PRINT_ERROR("Failed to create draw signal");
        goto err_draw_signal;
    }

    ScreenLock = xSemaphoreCreateMutex();
    if (!ScreenLock) {
        PRINT_ERROR("Failed to create screen lock");
        goto err_screen_lock;
    }

    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    Task21Timer = xTimerCreate('Task21timer', PERIOD_TASK21 / portTICK_PERIOD_MS / 2,
                                pdTRUE, pvTimerID, vTask21TimerCallback);
    if (!Task21Timer) {
        PRINT_ERROR("Could not create tast21 timer");
        goto err_task21_timer;
    }

    Task22Timer = xTimerCreate('Task22timer', pdMS_TO_TICKS(PERIOD_TASK22 / 2),
                                pdTRUE, pvTimerID, vTask22TimerCallback);
    if (!Task22Timer) {
        PRINT_ERROR("Could not create task22 timer");
        goto err_task22_timer;
    }

    if (xTaskCreate(basicSequentialStateMachine, "StateMachine",
                    mainGENERIC_STACK_SIZE * 2, NULL,
                    configMAX_PRIORITIES - 2, &StateMachine) != pdPASS) {
        PRINT_TASK_ERROR("StateMachine");
        goto err_statemachine;
    }

    if (xTaskCreate(vSwapBuffers, "BufferSwapTask",
                    mainGENERIC_STACK_SIZE * 2, NULL, configMAX_PRIORITIES - 1,
                    &BufferSwap) != pdPASS) {
        PRINT_TASK_ERROR("BufferSwapTask");
        goto err_bufferswap;
    }

    if (xTaskCreate(vTask1, "Task1", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task1) != pdPASS) {
        goto err_Task1;
    }

    if (xTaskCreate(vTask21, "Task21", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task21) != pdPASS) {
        goto err_Task21;
    }

    if (xTaskCreate(vTask22, "Task22", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task22) != pdPASS) {
        goto err_Task22;
    }

    vTaskSuspend(Task1);
    vTaskSuspend(Task21);
    vTaskSuspend(Task22);

    tumFUtilPrintTaskStateList();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_Task22:
    vTaskDelete(Task21);
err_Task21:
    vTaskDelete(Task1);
err_Task1:
    vTaskDelete(BufferSwap);
err_bufferswap:
    vTaskDelete(StateMachine);
err_statemachine:
    xTimerDelete(Task22Timer, 0); 
err_task22_timer:
    xTimerDelete(Task21Timer, 0);
err_task21_timer:
    vQueueDelete(StateQueue);
err_state_queue:
    vSemaphoreDelete(ScreenLock);
err_screen_lock:
    vSemaphoreDelete(DrawSignal);
err_draw_signal:
    vSemaphoreDelete(myABCDcounter.lock);
err_myABCDcounter_lock:
    vSemaphoreDelete(buttons.lock);
err_buttons_lock:
    tumSoundExit();
err_init_audio:
    tumEventExit();
err_init_events:
    tumDrawExit();
err_init_drawing:
    return EXIT_FAILURE;
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vMainQueueSendPassed(void)
{
    /* This is just an example implementation of the "queue send" trace hook. */
}

// cppcheck-suppress unusedFunction
__attribute__((unused)) void vApplicationIdleHook(void)
{
#ifdef __GCC_POSIX__
    struct timespec xTimeToSleep, xTimeSlept;
    /* Makes the process more agreeable when using the Posix simulator. */
    xTimeToSleep.tv_sec = 1;
    xTimeToSleep.tv_nsec = 0;
    nanosleep(&xTimeToSleep, &xTimeSlept);
#endif
}
