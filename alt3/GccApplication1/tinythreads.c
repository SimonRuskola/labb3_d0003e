#include <setjmp.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "tinythreads.h"

#define NULL            0
#define DISABLE()       cli()
#define ENABLE()        sei()
#define STACKSIZE       80
#define NTHREADS        4
#define SETSTACK(buf,a) *((unsigned int *)(buf)+8) = (unsigned int)(a) + STACKSIZE - 4; \
                        *((unsigned int *)(buf)+9) = (unsigned int)(a) + STACKSIZE - 4

struct thread_block {
    void (*function)(int);   // code to run
    int arg;                 // argument to the above
    thread next;             // for use in linked lists
    jmp_buf context;         // machine state
    char stack[STACKSIZE];   // execution stack space
};

struct thread_block threads[NTHREADS];

struct thread_block initp;

thread freeQ   = threads;
thread readyQ  = NULL;
thread current = &initp;

int initialized = 0;
int blinkCounter = 0;

mutex m_primes  = {1,0};
mutex m_button =  {1,0};
mutex m_blink =  {1,0};


mutex* getPrimesMutex(void){
    return &m_primes;
}
mutex* getButtonMutex(void){
    return &m_button;
}
mutex* getBlinkMutex(void){
    return &m_blink;
}

static void initialize(void) {
    int i;
    for (i=0; i<NTHREADS-1; i++)
        threads[i].next = &threads[i+1];
    threads[NTHREADS-1].next = NULL;


    // initialize button
    PORTB = PORTB | (1 << 7);
	// enable interupts for joystick
	EIMSK = EIMSK | (1 << PCINT15);
	PCMSK1 = PCMSK1 | (1 << PCINT15);



	
    CLKPR  |= (1 << CLKPS0);  //8 MHz system clock

    TCCR1B |= (1 << WGM12); // Sets the timer to CTC mode

	TCCR1B |= (1<<CS12) | (1<<CS10); //prescaling factor of 1024
    
    TIMSK1 |=  (1 << OCIE1A); //enabe interupts for timer

    OCR1A = 3910; // (8000000 / (1024) * 500 *10^(-3)  

    TCNT1 = 0; // set timer to 0
    

    initialized = 1;
}

void button(int);

ISR(PCINT1_vect) { 
	spawn(button,4);
}


void blink(int);

ISR(TIMER1_COMPA_vect){
    blinkCounter++;
	spawn(blink,1);
    
}






static void enqueue(thread p, thread *queue) {
    p->next = *queue;
    *queue = p;
        
}



static thread dequeue(thread *queue) {
    thread p = *queue;
    if (*queue) {
        *queue = (*queue)->next;
    } else {
        // Empty queue, kernel panic!!!
        while (1) ;  // not much else to do...
    }
    return p;
}

static void dispatch(thread next) {
    if (setjmp(current->context) == 0) {
        current = next;
        longjmp(next->context,1);
    }
}



void spawn(void (* function)(int), int arg) {
    thread newp;

    DISABLE();
    if (!initialized) initialize();

    newp = dequeue(&freeQ);
    newp->function = function;
    newp->arg = arg;
    newp->next = NULL;
    if (setjmp(newp->context) == 1) {
        ENABLE();
        current->function(current->arg);
        DISABLE();
        enqueue(current, &freeQ);
        dispatch(dequeue(&readyQ));
    }
    SETSTACK(&newp->context, &newp->stack);

    //enqueue(newp, &readyQ);
    enqueue(current, &readyQ);
    dispatch(newp);
    

    
    ENABLE();
}

void yield(void) {
	DISABLE();
	enqueue(current,&readyQ);
	dispatch(dequeue(&readyQ));
	ENABLE();
	
}

void lock(mutex *m) {
	DISABLE();
    if(m->locked == 0){
        m->locked = 1;
    } else{
        enqueue(current,&(m->waitQ));
        dispatch(dequeue(&readyQ));
    }
	ENABLE();
}
void unlock(mutex *m) {
	DISABLE();
    if(m->waitQ){
        enqueue(current,&readyQ);
        dispatch(dequeue(&(m->waitQ)));
    }else{
        m->locked = 0; 
    }
	ENABLE();

}
