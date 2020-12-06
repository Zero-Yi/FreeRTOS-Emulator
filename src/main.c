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
#include "event_groups.h"

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
#define STATE_QUEUE_LENGTH  1
#define STATE_COUNT         4
#define STATE_ONE           0
#define STATE_TWO           1
#define STATE_THREE         2
#define STATE_FOUR          3
#define NEXT_TASK           0
#define PREV_TASK           1
#define STARTING_STATE      STATE_ONE
#define STATE_DEBOUNCE_DELAY 300
#define STATE_CHANGING      1

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

#define BIT_0 ( 1U << 0U )
#define BIT_1 ( 1U << 1U )
#define BIT_2 ( 1U << 2U )
#define BIT_3 ( 1U << 3U )
#define BIT_4 ( 1U << 4U )
#define BIT_5 ( 1U << 5U )
#define BIT_6 ( 1U << 6U )
#define BIT_7 ( 1U << 7U )
#define BIT_8 ( 1U << 8U )
#define BIT_9 ( 1U << 9U )
#define BIT_10 ( 1U << 10U )

#define MOVE_WITH_MOUSE 1

#define A_COUNTER_POSITION 0
#define B_COUNTER_POSITION 1
#define C_COUNTER_POSITION 2
#define D_COUNTER_POSITION 3
#define NUMBER_OF_TRACED_BUTTONS 4

// period in ms
#define PERIOD_TASK211 1000
#define PERIOD_TASK212 500
#define PERIOD_TASK22  1000

#define STATIC_TASK211_STACKSIZE (StackType_t)mainGENERIC_STACK_SIZE * 2

static QueueHandle_t StateQueue = NULL;
static SemaphoreHandle_t DrawSignal = NULL;
static SemaphoreHandle_t ScreenLock = NULL;

static TaskHandle_t StateMachine = NULL;
static TaskHandle_t BufferSwap = NULL;
static TaskHandle_t Task1 = NULL;

static TaskHandle_t Task211 = NULL;
static TaskHandle_t Task212 = NULL;
static StaticTask_t Task211Buffer;
static TimerHandle_t Task211Timer = NULL;
static TimerHandle_t Task212Timer = NULL;

static TaskHandle_t Task221 = NULL;
static TaskHandle_t Task222 = NULL;
static TaskHandle_t Task22aux = NULL;
static SemaphoreHandle_t Task221BinSema = NULL;
static TimerHandle_t Task22Timer = NULL;

static TaskHandle_t Task23 = NULL;
static TaskHandle_t Task23aux = NULL;

static StackType_t xStack[ STATIC_TASK211_STACKSIZE ];
static EventGroupHandle_t myEventGroup = NULL;
static EventGroupHandle_t stateChangingSync = NULL;

const unsigned char next_state_signal = NEXT_TASK;
const unsigned char prev_state_signal = PREV_TASK;

// const char * const pvTimerID = 0;

typedef struct buttons_buffer {
    unsigned char buttons[SDL_NUM_SCANCODES];
    SemaphoreHandle_t lock;
} buttons_buffer_t;

typedef struct state_global {
    volatile unsigned char current_state;
    volatile unsigned short state_changing_signal;
    SemaphoreHandle_t lock;
} state_global_t;

typedef struct ABCDcounter {
    short counter[4]; 
    SemaphoreHandle_t lock;
} ABCDcounter_t;

typedef struct Task23counter {
    unsigned short counter; 
    SemaphoreHandle_t lock;
} Task23counter_t;

static buttons_buffer_t buttons = { 0 };
static state_global_t state = {STARTING_STATE, 0, NULL};
static ABCDcounter_t myABCDcounter = { 0 };
static Task23counter_t task23counter = {0, NULL };

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    uint32_t *pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     uint32_t *pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}                                     

void xGetButtonInput(void)
{
    if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
        xQueueReceive(buttonInputQueue, &buttons.buttons, 0);
        xSemaphoreGive(buttons.lock);
    }
    // else
        // prints("fail to get lock in xGetButtonInput\n");
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
    unsigned short current_state = 0;

    tumDrawBindThread(); // Setup Rendering handle with correct GL context

    while (1) {
        xSemaphoreTake(state.lock, portMAX_DELAY);
        current_state = state.current_state;
        xSemaphoreGive(state.lock);

        if (xSemaphoreTake(ScreenLock, portMAX_DELAY) == pdTRUE) {
            tumDrawUpdateScreen();
            tumEventFetchEvents(FETCH_EVENT_BLOCK);
            checkDraw(tumDrawClear(White), __FUNCTION__);
            xSemaphoreGive(ScreenLock);

            switch (current_state){
                case STATE_ONE:
                    xSemaphoreGive(DrawSignal);
                    break;
                case STATE_TWO:
                    xEventGroupSetBits(myEventGroup, BIT_0|BIT_1);                        
                    break;
                case STATE_THREE:
                    xEventGroupSetBits(myEventGroup, BIT_2);                        
                    break;            
                case STATE_FOUR:
                    xEventGroupSetBits(myEventGroup, BIT_4);                        
                    break;
                default:
                    break;
            }

            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(frameratePeriod));
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
                prints("E pressed\n");
                xQueueSend(StateQueue, &next_state_signal, 0);
                /* prints("E pressed\n");  strange that if the prints stands here, 
                                            then nothing is going to be printed.*/
                return STATE_CHANGING;
            }
            xSemaphoreGive(buttons.lock);
            return -1;
        }
        xSemaphoreGive(buttons.lock);
    }
    // else
        // prints("fail to get lock in vCheckStateInput\n");
    return 0;
}

/*
 * Example basic state machine with sequential states
 */
void basicSequentialStateMachine(void *pvParameters)
{
    unsigned char state_changed =
        1; // Only re-evaluate state if it has changed
    unsigned char input = 0;
    EventBits_t bitToWait = 0;
    const int state_change_period = STATE_DEBOUNCE_DELAY;

    TickType_t last_change = xTaskGetTickCount();

    while (1) {
        // prints("here\n");
        if (state_changed) {
            goto initial_state;
        }
        // xGetButtonInput();
        // vCheckStateInput();
        // prints("StateInput checked\n");

        // if (vCheckStateInput() == STATE_CHANGING)
        //     if (xTaskGetTickCount() - last_change > state_change_period) {

        //         xSemaphoreTake(state.lock, portMAX_DELAY);
        //         changeState(&state.current_state, input);
        //         state.state_changing_signal = STATE_CHANGING;
        //         xSemaphoreGive(state.lock);

        //         state_changed = 1;
        //         last_change = xTaskGetTickCount();
        // }

        // Handle state machine input
        if (StateQueue)
            if (xQueueReceive(StateQueue, &input, portMAX_DELAY) ==
                pdTRUE)
                if (xTaskGetTickCount() - last_change >
                    state_change_period) {

                    xSemaphoreTake(state.lock, portMAX_DELAY);
                    switch (state.current_state){
                    case STATE_ONE:
                        bitToWait = BIT_2;
                        break;
                    case STATE_TWO:
                        bitToWait = BIT_3|BIT_4;
                        break;
                    case STATE_THREE:
                        bitToWait = BIT_5|BIT_6|BIT_7;
                        break;
                    case STATE_FOUR:
                        bitToWait = BIT_8|BIT_9;
                        break;
                    default:
                        break;
                    }
                    changeState(&state.current_state, input);
                    state.state_changing_signal = STATE_CHANGING;
                    xSemaphoreGive(state.lock);

                    state_changed = 1;
                    last_change = xTaskGetTickCount();

                    // sync point
                    xEventGroupSync(stateChangingSync, BIT_1, 
                                    BIT_1|bitToWait, portMAX_DELAY);
                }

    initial_state:
        // Handle current state
        if (state_changed) {
            xSemaphoreTake(state.lock, portMAX_DELAY);
            switch (state.current_state) {
                case STATE_ONE:
                        vTaskResume(Task1);

                        vTaskSuspend(Task211);
                        vTaskSuspend(Task212);
                        xTimerStop(Task211Timer, portMAX_DELAY);
                        xTimerStop(Task212Timer, portMAX_DELAY);

                        vTaskSuspend(Task221);
                        vTaskSuspend(Task222);
                        vTaskSuspend(Task22aux);
                        xTimerStop(Task22Timer, portMAX_DELAY);

                        vTaskSuspend(Task23);
                        vTaskSuspend(Task23aux);
                        prints("Change to state 1\n");
                    break;
                case STATE_TWO:
                        vTaskSuspend(Task1);

                        vTaskResume(Task211);
                        vTaskResume(Task212);
                        xTimerStart(Task211Timer, portMAX_DELAY);
                        xTimerStart(Task212Timer, portMAX_DELAY);

                        vTaskSuspend(Task221);
                        vTaskSuspend(Task222);
                        vTaskSuspend(Task22aux);    
                        xTimerStop(Task22Timer, portMAX_DELAY);      

                        vTaskSuspend(Task23);
                        vTaskSuspend(Task23aux);
                        prints("Change to state 2\n");
                    break;
                case STATE_THREE:
                        vTaskSuspend(Task1);

                        vTaskSuspend(Task211);
                        vTaskSuspend(Task212);
                        xTimerStop(Task211Timer, portMAX_DELAY);
                        xTimerStop(Task212Timer, portMAX_DELAY);

                        vTaskResume(Task221);
                        vTaskResume(Task222);
                        vTaskResume(Task22aux);      
                        xTimerStart(Task22Timer, portMAX_DELAY);    

                        vTaskSuspend(Task23);
                        vTaskSuspend(Task23aux);
                        prints("Change to state 3\n");
                    break;                    
                case STATE_FOUR:
                        vTaskSuspend(Task1);

                        vTaskSuspend(Task211);
                        vTaskSuspend(Task212);
                        xTimerStop(Task211Timer, portMAX_DELAY);
                        xTimerStop(Task212Timer, portMAX_DELAY);

                        vTaskSuspend(Task221);
                        vTaskSuspend(Task222);
                        vTaskSuspend(Task22aux);        
                        xTimerStop(Task22Timer, portMAX_DELAY);       

                        // vTaskResume(Task23);
                        vTaskResume(Task23aux);
                        prints("Change to state 4\n");
                    break;
                default:
                    break;
            }
            state.state_changing_signal = 0;
            xSemaphoreGive(state.lock);
            state_changed = 0;
        }
        // prints("State Machine loop over\n");
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
    else
        prints("fail to get lock in countTheABCDs\n");

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
    unsigned short initial = 1; 
    // when from Blocked/Suspended to running, turn to 1;
    // prints("test\n");

    while (1) {
        if (DrawSignal)
            if (xSemaphoreTake(DrawSignal, portMAX_DELAY) ==
                pdTRUE) {
                if ( initial == 1 ){
                    prevWakeTime = xTaskGetTickCount();
                    initial = 0;
                }

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
        
        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_2, BIT_1|BIT_2, portMAX_DELAY);

            initial = 1;
        }
        else   
            xSemaphoreGive(state.lock);
    }
}

void vTask211TimerCallback( TimerHandle_t xTimer )
{
    xTaskNotifyGive(Task211);
    // prints("task21 timer expires!\n");
}

void vTask212TimerCallback( TimerHandle_t xTimer )
{
    xTaskNotifyGive(Task212);
    // prints("task22 timer expires!\n");
}

#define Task211Circle_X SCREEN_WIDTH / 4
#define Task211Circle_Y SCREEN_HEIGHT / 2
#define Task212Circle_X SCREEN_WIDTH / 4 * 3
#define Task212Circle_Y SCREEN_HEIGHT / 2
#define Task2Circle_Radius 60

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

void vTask211(void *pvParameters)
{
    unsigned short flag = 0;
    // unsigned long flag = 0;
    // unsigned int waterMark = 0;
    // u_int32_t notificationValue = 0;

    while (1) {
        if ( ulTaskNotifyTake(pdTRUE, 0) > 0 ) // check the toggle singal
            if( flagToggle(&flag) == -1)
                prints("Error! Task211 toggle flag not 1 or 0\n");

        xEventGroupWaitBits(myEventGroup, BIT_0, pdTRUE, pdTRUE, portMAX_DELAY);

        xGetButtonInput(); // Update agency

        if (flag == 1){ // on drawing phase
            checkDraw(tumDrawCircle(Task211Circle_X, Task211Circle_Y, 
                                Task2Circle_Radius, Orange), __FUNCTION__);
            // prints("Draw!\n");
        }

        vCheckStateInput();
        // waterMark = uxTaskGetStackHighWaterMark(NULL);
        // prints("Task211 hight water mark is:%d\n", waterMark);

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_3, 
                            BIT_1|BIT_3|BIT_4, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);
    }
}

void vTask212(void *pvParameters)
{
    static unsigned short flag = 0;

    while (1) {
        if ( ulTaskNotifyTake(pdTRUE, 0) > 0 ) // check the toggle singal
            if( flagToggle(&flag) == -1)
                prints("Error! Task212 toggle flag not 1 or 0\n");

        xEventGroupWaitBits(myEventGroup, BIT_1, pdTRUE, pdTRUE, portMAX_DELAY);

        xGetButtonInput(); // Update agency
        
        if (flag == 1) // on drawing phase
            checkDraw(tumDrawCircle(Task212Circle_X, Task212Circle_Y, 
                                Task2Circle_Radius, Orange), __FUNCTION__);
                            
        vCheckStateInput();

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_4, 
                            BIT_1|BIT_3|BIT_4, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);
    }
}

void vTask22TimerCallback( TimerHandle_t xTimer)
{
    xTaskNotify(Task22aux, BIT_0, eSetBits);
    // prints("counting down Task22Timer!\n");
}

void vTask221(void *pvParameters)
{
    while(1){
        // prints("Task221 running\n");
        xGetButtonInput(); // Update agency
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){
            // printf("got the lock 221\n");
            if (buttons.buttons[KEYCODE(1)]) { // press "1" increase the counter
                buttons.buttons[KEYCODE(1)] = 0;
                xSemaphoreGive(buttons.lock);
                xSemaphoreGive(Task221BinSema);
                // prints("1 pressed!\n");
            }
            else
                xSemaphoreGive(buttons.lock);
        }
        vCheckStateInput();

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_5, 
                            BIT_1|BIT_5|BIT_6|BIT_7, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);

        vTaskDelay(5); // so that the lower priority task can run
    }
}

void vTask222(void *pvParameters)
{
    while(1){
        xGetButtonInput(); // Update agency
        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE){
            // printf("got the lock 221\n");
            if (buttons.buttons[KEYCODE(2)]) { // press "2" increase the counter
                buttons.buttons[KEYCODE(2)] = 0;
                xSemaphoreGive(buttons.lock);
                xTaskNotify(Task22aux, BIT_1, eSetBits);
                // prints("2 pressed!\n");
            }
            else
                xSemaphoreGive(buttons.lock);
        }
        vCheckStateInput();

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_6, 
                            BIT_1|BIT_5|BIT_6|BIT_7, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);
        
        vTaskDelay(5); // so that the lower priority task can run
    }
}

void vTask22aux(void *pvParameters)
{
    unsigned short counter1 = 0, counter2 = 0, counterReset = 15;
    uint32_t notificationValue = 0;

    char str[50] = { 0 };
    int text_width;
    ssize_t prev_font_size;
    ssize_t current_font_size = (ssize_t)30;

    while(1){

        if (xSemaphoreTake(Task221BinSema, 0) == pdTRUE) // 1 pressed
            counter1 ++;

        if (xTaskNotifyWait(0, BIT_0|BIT_1, &notificationValue, 0) == pdTRUE){
            if( notificationValue == 1 ){
                // notification from the timer, to count down.
                counterReset --;
                // prints("counting down Task22aux!\n");
            } 
            else if ( notificationValue == 2 )// notification from the task222, to increas the counter2
                counter2 ++;
            else
                prints("Error! Task22aux unknown notification value!\n");
        }

        if (counterReset <= 0){
                counter1 = 0;
                counter2 = 0;   
                counterReset = 15;         
        }

        prev_font_size = tumFontGetCurFontSize();
        tumFontSetSize(current_font_size);
        xEventGroupWaitBits(myEventGroup, BIT_2, pdTRUE, pdTRUE, portMAX_DELAY);

        sprintf(str, "Counter[1]:%hd Counter[2]: %hd", counter1, counter2);
        if (!tumGetTextSize((char *)str, &text_width, NULL))
            checkDraw(tumDrawText(str, 
                            SCREEN_WIDTH / 2 - text_width / 2,
                            SCREEN_HEIGHT / 2 - current_font_size * 2.5,
                            Fuchsia), __FUNCTION__);

        sprintf(str, "Reset counting down:%hd", counterReset);
        if (!tumGetTextSize((char *)str, &text_width, NULL))
            checkDraw(tumDrawText(str, 
                            SCREEN_WIDTH / 2 - text_width / 2,
                            SCREEN_HEIGHT / 2,
                            Fuchsia), __FUNCTION__);

        if ( counterReset == 1){
            sprintf(str, "Reseting!");
            if (!tumGetTextSize((char *)str, &text_width, NULL))
                checkDraw(tumDrawText(str, 
                                SCREEN_WIDTH / 2 - text_width / 2,
                                SCREEN_HEIGHT / 2 +  current_font_size * 1.5,
                                Red), __FUNCTION__);
        }

        tumFontSetSize(prev_font_size);    

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_7, 
                            BIT_1|BIT_5|BIT_6|BIT_7, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);                                
    }
    
}

void vTask23(void *pvParameters)
{
    TickType_t xLastWakeTime;
    const TickType_t counterPeriod = 1000; // Period by ms

    while(1){
        if ( xSemaphoreTake(task23counter.lock, portMAX_DELAY) == pdTRUE ){
            task23counter.counter ++;
            // prints("task23 counter ++!\n");
            xSemaphoreGive(task23counter.lock);
        }
        xLastWakeTime = xTaskGetTickCount();
        
        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            // sync point
            xEventGroupSync(stateChangingSync, BIT_8, 
                            BIT_1|BIT_8|BIT_9, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(counterPeriod));
    }
}

void vTask23aux(void *pvParameters)
{
    unsigned short flag = 0; // flag stands for pause

    char str[50] = { 0 };
    int text_width;
    ssize_t prev_font_size;
    ssize_t current_font_size = (ssize_t)30;

    while(1){

        xEventGroupWaitBits(myEventGroup, BIT_4, pdTRUE, pdTRUE, portMAX_DELAY);
        xGetButtonInput(); // Update agency

        prev_font_size = tumFontGetCurFontSize();
        tumFontSetSize(current_font_size);

        xSemaphoreTake(task23counter.lock, portMAX_DELAY);
        sprintf(str, "The counter say: %hd, [P]ause", task23counter.counter);
        xSemaphoreGive(task23counter.lock);

        if (!tumGetTextSize((char *)str, &text_width, NULL))
            checkDraw(tumDrawText(str, 
                                SCREEN_WIDTH / 2 - text_width / 2,
                                SCREEN_HEIGHT / 2 - current_font_size * 2, Black),
                                __FUNCTION__);

        if ( flag == 1 ){                        
            sprintf(str, "Paused");
            if (!tumGetTextSize((char *)str, &text_width, NULL))
                checkDraw(tumDrawText(str, 
                                SCREEN_WIDTH / 2 - text_width / 2,
                                SCREEN_HEIGHT / 2 ,
                                Fuchsia), __FUNCTION__);
        }

        if (xSemaphoreTake(buttons.lock, 0) == pdTRUE) {
            // prints("success to get lock\n");
            if (buttons.buttons[KEYCODE(P)]) { // press "P" to pause the counter
                buttons.buttons[KEYCODE(P)] = 0;

                xSemaphoreGive(buttons.lock);

                // printf("Paused!\n");
                // if ( flag == 0 ) {
                //     flag = 1;
                //     vTaskSuspend(Task23);
                // }
                // else if ( flag == 1 ){
                //     flag = 0;
                //     vTaskResume(Task23);
                // }      
                flagToggle(&flag);        
            }
            else
                xSemaphoreGive(buttons.lock); // rememer to return the key
        }
        else
            prints("fail to get lock\n");

        if ( flag == 0 ) {
            vTaskResume(Task23);
        }
        else if ( flag == 1 ){
            vTaskSuspend(Task23);
        }   

        tumFontSetSize(prev_font_size);
        vCheckStateInput();

        xSemaphoreTake(state.lock, 0);
        if( state.state_changing_signal == STATE_CHANGING){
            xSemaphoreGive(state.lock);
            vTaskResume(Task23); // to let it go to the sync point
            // sync point
            xEventGroupSync(stateChangingSync, BIT_9, 
                            BIT_1|BIT_8|BIT_9, portMAX_DELAY);
        }
        else   
            xSemaphoreGive(state.lock);
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

    state.lock = xSemaphoreCreateMutex(); // Locking mechanism
    if (!state.lock) {
        PRINT_ERROR("Failed to create state lock");
        goto err_state_lock;
    }

    task23counter.lock = xSemaphoreCreateMutex();
    if (!task23counter.lock) {
        PRINT_ERROR("Failed to create task23counter lock");
        goto err_task23counter_lock;
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

    Task221BinSema = xSemaphoreCreateBinary();
    if (!Task221BinSema) {
        PRINT_ERROR("Failed to create Task221BinSema");
        goto err_Task221BinSema;
    }

    StateQueue = xQueueCreate(STATE_QUEUE_LENGTH, sizeof(unsigned char));
    if (!StateQueue) {
        PRINT_ERROR("Could not open state queue");
        goto err_state_queue;
    }

    myEventGroup = xEventGroupCreate();
    if (!myEventGroup) {
        PRINT_ERROR("Could not create event group");
        goto err_event_group;
    }

    stateChangingSync = xEventGroupCreate();
    if (!stateChangingSync) {
        PRINT_ERROR("Could not create stateChangingSync event group");
        goto err_stateChangingSync;
    }

    Task211Timer = xTimerCreate("Task211timer", PERIOD_TASK211 / portTICK_PERIOD_MS / 2,
                                pdTRUE, 0, vTask211TimerCallback);
    if (!Task211Timer) {
        PRINT_ERROR("Could not create tast211 timer");
        goto err_task211_timer;
    }

    Task212Timer = xTimerCreate("Task212timer", pdMS_TO_TICKS(PERIOD_TASK212 / 2),
                                pdTRUE, 0, vTask212TimerCallback);
    if (!Task212Timer) {
        PRINT_ERROR("Could not create task212 timer");
        goto err_task212_timer;
    }

    Task22Timer = xTimerCreate("Task22Timer", pdMS_TO_TICKS(PERIOD_TASK22),
                                pdTRUE, 0, vTask22TimerCallback);
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
        PRINT_TASK_ERROR("Task1");
        goto err_Task1;
    }

    // if (xTaskCreate(vTask211, "Task211", mainGENERIC_STACK_SIZE * 2, NULL,
    //                 mainGENERIC_PRIORITY + 1, &Task211) != pdPASS) {
    //     goto err_Task211;
    // }

    Task211 = xTaskCreateStatic(vTask211, "Task211", 0, NULL,
                mainGENERIC_PRIORITY + 1, xStack, &Task211Buffer);
    if ( Task211 == NULL) {
        PRINT_TASK_ERROR("Task211");
        goto err_Task211;
    }

    if (xTaskCreate(vTask212, "Task212", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task212) != pdPASS) {
        PRINT_TASK_ERROR("Task212");
        goto err_Task212;
    }

    if (xTaskCreate(vTask221, "Task221", configMINIMAL_STACK_SIZE, NULL,
                    mainGENERIC_PRIORITY + 1, &Task221) != pdPASS) {
        PRINT_TASK_ERROR("Task221");
        goto err_Task221;
    }

    if (xTaskCreate(vTask222, "Task222", configMINIMAL_STACK_SIZE, NULL,
                    mainGENERIC_PRIORITY + 2, &Task222) != pdPASS) {
        PRINT_TASK_ERROR("Task222");
        goto err_Task222;
    }

    if (xTaskCreate(vTask22aux, "Task22aux", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task22aux) != pdPASS) {
        PRINT_TASK_ERROR("Task22aux");
        goto err_Task22aux;
    }

    if (xTaskCreate(vTask23, "Task23", configMINIMAL_STACK_SIZE, NULL,
                    mainGENERIC_PRIORITY, &Task23) != pdPASS) {
        PRINT_TASK_ERROR("Task23");
        goto err_Task23;
    }

    if (xTaskCreate(vTask23aux, "Task23aux", mainGENERIC_STACK_SIZE * 2, NULL,
                    mainGENERIC_PRIORITY, &Task23aux) != pdPASS) {
        PRINT_TASK_ERROR("Task23aux");
        goto err_Task23aux;
    }

    vTaskSuspend(Task1);
    vTaskSuspend(Task211);
    vTaskSuspend(Task212);
    vTaskSuspend(Task221);
    vTaskSuspend(Task222);
    vTaskSuspend(Task22aux);    
    vTaskSuspend(Task23);
    vTaskSuspend(Task23aux);

    tumFUtilPrintTaskStateList();

    vTaskStartScheduler();

    return EXIT_SUCCESS;

err_Task23aux:
    vTaskDelete(Task23);
err_Task23:
    vTaskDelete(Task22aux);
err_Task22aux:
    vTaskDelete(Task222);
err_Task222:
    vTaskDelete(Task221);
err_Task221:
    vTaskDelete(Task212);
err_Task212:
    vTaskDelete(Task211);
err_Task211:
    vTaskDelete(Task1);
err_Task1:
    vTaskDelete(BufferSwap);
err_bufferswap:
    vTaskDelete(StateMachine);
err_statemachine:
    xTimerDelete(Task22Timer, 0); 
err_task22_timer:
    xTimerDelete(Task212Timer, 0); 
err_task212_timer:
    xTimerDelete(Task211Timer, 0);
err_task211_timer:
    vEventGroupDelete(stateChangingSync);
err_stateChangingSync:
    vEventGroupDelete(myEventGroup);
err_event_group:
    vQueueDelete(StateQueue);
err_state_queue:
    vSemaphoreDelete(Task221BinSema);
err_Task221BinSema:
    vSemaphoreDelete(ScreenLock);
err_screen_lock:
    vSemaphoreDelete(DrawSignal);
err_draw_signal:
    vSemaphoreDelete(task23counter.lock);
err_task23counter_lock:
    vSemaphoreDelete(state.lock);
err_state_lock:
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
