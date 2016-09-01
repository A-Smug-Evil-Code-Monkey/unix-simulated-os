/* ------------------------------------------------------------------------
   phase1.c

   University of Arizona
   Computer Science 452
   Fall 2016

   @author Austin George
   @author Joshua Riccio
   ------------------------------------------------------------------------ */

#include "phase1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kernel.h"

/* ------------------------- Prototypes ----------------------------------- */
int sentinel (char *);
extern int start1 (char *);
void dispatcher(void);
void launch();
static void checkDeadlock();
void addProcToReadyList(procPtr proc);
void printReadyList();
int getProcSlot();
void initProcStruct(int index);
/* -------------------------- Globals ------------------------------------- */

// Patrick's debugging global variable...
int debugflag = 1;

// the process table
procStruct ProcTable[MAXPROC];
unsigned short currentPID = 0; // last used pid

// Process lists
static procPtr ReadyList;

// current process ID
procPtr Current;

// the next pid to be assigned
unsigned int nextPid = SENTINELPID;


/* -------------------------- Functions ----------------------------------- */
/* ------------------------------------------------------------------------
   Name - startup
   Purpose - Initializes process lists and clock interrupt vector.
             Start up sentinel process and the test process.
   Parameters - none, called by USLOSS
   Returns - nothing
   Side Effects - lots, starts the whole thing
   ----------------------------------------------------------------------- */
void startup()
{
    int result; // value returned by call to fork1()

    // initialize the process table
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing process table, ProcTable[]\n");
    for (int i = 0; i < 50; i++) {
        initProcStruct(i);
    }

    // Initialize the Ready list, etc.
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): initializing the Ready list\n");
    ReadyList = NULL;

    // Initialize the clock interrupt handler

    // startup a sentinel process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for sentinel\n");
    result = fork1("sentinel", sentinel, NULL, USLOSS_MIN_STACK,
                    SENTINELPRIORITY);
    if (result < 0) {
        if (DEBUG && debugflag) {
            USLOSS_Console("startup(): fork1 of sentinel returned error, ");
            USLOSS_Console("halting...\n");
        }
        USLOSS_Halt(1);
    }
  
    // start the test process
    if (DEBUG && debugflag)
        USLOSS_Console("startup(): calling fork1() for start1\n");
    result = fork1("start1", start1, NULL, 2 * USLOSS_MIN_STACK, 1);
    if (result < 0) {
        USLOSS_Console("startup(): fork1 for start1 returned an error, ");
        USLOSS_Console("halting...\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("startup(): Should not see this message! ");
    USLOSS_Console("Returned from fork1 call that created start1\n");

    return;
} /* startup */

/* ------------------------------------------------------------------------
   Name - finish
   Purpose - Required by USLOSS
   Parameters - none
   Returns - nothing
   Side Effects - none
   ----------------------------------------------------------------------- */
void finish()
{
    if (DEBUG && debugflag)
        USLOSS_Console("in finish...\n");
} /* finish */

/* ------------------------------------------------------------------------
   Name - fork1
   Purpose - Gets a new process from the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.
   Parameters - the process procedure address, the size of the stack and
                the priority to be assigned to the child process.
   Returns - the process id of the created child or -1 if no child could
             be created or if priority is not between max and min priority.
   Side Effects - ReadyList is changed, ProcTable is changed, Current
                  process information changed
   ------------------------------------------------------------------------ */
int fork1(char *name, int (*startFunc)(char *), char *arg,
          int stacksize, int priority)
{
    int procSlot = -1;
    struct psrBits psr;  //TODO: verify var name

    if (DEBUG && debugflag)
        USLOSS_Console("fork1(): creating process %s\n", name);

    // test if in kernel mode; halt if in user mode
    psrInit(&psr, USLOSS_PsrGet());
    if (psr.curMode == 1) {
        if (DEBUG && debugflag) {
            USLOSS_Console("fork1(): User %s is in kernal mode.\n", name);
        }
    } else {
        if (DEBUG && debugflag) {
            USLOSS_Console("fork1(): User %s is in user mode.\n", name);
        }
        USLOSS_Halt(1);
    }

    // Return if stack size is too small
    if (stacksize < USLOSS_MIN_STACK) {
        return -2;
    }

    // find an empty slot in the process table
    procSlot = getProcSlot();
    if (procSlot == -1) {
        if (DEBUG && debugflag) {
            USLOSS_Console("fork1(): Process %s - no empty slot.\n", 
                           name);
        }
        return -1;
    }

    if (DEBUG && debugflag) {
        USLOSS_Console("fork1(): Process %s PID equals %d.\n", 
                       name, currentPID);
    }

    // fill-in entry in process table */
    if ( strlen(name) >= (MAXNAME - 1) ) {
        USLOSS_Console("fork1(): Process name is too long.  Halting...\n");
        USLOSS_Halt(1);
    }
  
    strcpy(ProcTable[procSlot].name, name);
    ProcTable[procSlot].startFunc = startFunc; 
    if ( arg == NULL )
        ProcTable[procSlot].startArg[0] = '\0';
    else if ( strlen(arg) >= (MAXARG - 1) ) {
        USLOSS_Console("fork1(): argument too long.  Halting...\n");
        USLOSS_Halt(1);
    }
    else
        strcpy(ProcTable[procSlot].startArg, arg);

    ProcTable[procSlot].stackSize = stacksize;
    ProcTable[procSlot].stack = malloc(stacksize);
    ProcTable[procSlot].priority = priority;
    ProcTable[procSlot].startFunc = startFunc;
    
    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC)
    // TODO: procSlot.state value incorrect
    USLOSS_ContextInit(&(ProcTable[procSlot].state), USLOSS_PsrGet(),
                       ProcTable[procSlot].stack,
                       ProcTable[procSlot].stackSize,
                       launch);

    // for future phase(s)
    p1_fork(ProcTable[procSlot].pid);

    // More stuff to do here...

    //Add process to ready list
    addProcToReadyList(&ProcTable[procSlot]);

    currentPID++;

    return ProcTable[procSlot].pid;
} /* fork1 */

/* ------------------------------------------------------------------------
   Name - launch
   Purpose - Dummy function to enable interrupts and launch a given process
             upon startup.
   Parameters - none
   Returns - nothing
   Side Effects - enable interrupts
   ------------------------------------------------------------------------ */
void launch()
{
    int result;

    if (DEBUG && debugflag)
        USLOSS_Console("launch(): started\n");

    // Enable interrupts

    // Call the function passed to fork1, and capture its return value
    result = Current->startFunc(Current->startArg);

    if (DEBUG && debugflag)
        USLOSS_Console("Process %d returned to launch\n", Current->pid);

    quit(result);

} /* launch */


/* ------------------------------------------------------------------------
   Name - join
   Purpose - Wait for a child process (if one has been forked) to quit.  If 
             one has already quit, don't wait.
   Parameters - a pointer to an int where the termination code of the 
                quitting process is to be stored.
   Returns - the process id of the quitting child joined on.
             -1 if the process was zapped in the join
             -2 if the process has no children
   Side Effects - If no child process has quit before join is called, the 
                  parent is removed from the ready list and blocked.
   ------------------------------------------------------------------------ */
int join(int *status)
{
    return -1;  // -1 is not correct! Here to prevent warning.
} /* join */


/* ------------------------------------------------------------------------
   Name - quit
   Purpose - Stops the child process and notifies the parent of the death by
             putting child quit info on the parents child completion code
             list.
   Parameters - the code to return to the grieving parent
   Returns - nothing
   Side Effects - changes the parent of pid child completion status list.
   ------------------------------------------------------------------------ */
void quit(int status)
{
    p1_quit(Current->pid);
} /* quit */


/* ------------------------------------------------------------------------
   Name - dispatcher
   Purpose - dispatches ready processes.  The process with the highest
             priority (the first on the ready list) is scheduled to
             run.  The old process is swapped out and the new process
             swapped in.
   Parameters - none
   Returns - nothing
   Side Effects - the context of the machine is changed
   ----------------------------------------------------------------------- */
void dispatcher(void)
{
    procPtr nextProcess = NULL;

    p1_switch(Current->pid, nextProcess->pid);
} /* dispatcher */


/* ------------------------------------------------------------------------
   Name - sentinel
   Purpose - The purpose of the sentinel routine is two-fold.  One
             responsibility is to keep the system going when all other
             processes are blocked.  The other is to detect and report
             simple deadlock states.
   Parameters - none
   Returns - nothing
   Side Effects -  if system is in deadlock, print appropriate error
                   and halt.
   ----------------------------------------------------------------------- */
int sentinel (char *dummy)
{
    if (DEBUG && debugflag)
        USLOSS_Console("sentinel(): called\n");
    while (1)
    {
        checkDeadlock();
        USLOSS_WaitInt();
    }
} /* sentinel */


/* check to determine if deadlock has occurred... */
static void checkDeadlock()
{
} /* checkDeadlock */


/*
 * Disables the interrupts.
 */
void disableInterrupts()
{
    // turn the interrupts OFF iff we are in kernel mode
    if( (USLOSS_PSR_CURRENT_MODE & USLOSS_PsrGet()) == 0 ) {
        //not in kernel mode
        USLOSS_Console("Kernel Error: Not in kernel mode, may not ");
        USLOSS_Console("disable interrupts\n");
        USLOSS_Halt(1);
    } else
        // We ARE in kernel mode
        USLOSS_PsrSet( USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_INT );
} /* disableInterrupts */

/*---------------------------- addProcToReadyList -----------------------
|  Function addProcToReadyList
|
|  Purpose:  Adds a new process to the ready list. Process is added to 
|            the list based on priority. Lower priorities are placed at
|            the front of the list.
|
|  Parameters:  proc (IN) -- The process to be added to the ready list.
|
|  Returns:  None
|
|  Side Effects:  proc is added to the correct location in ready list.
*-------------------------------------------------------------------*/
void addProcToReadyList(procPtr proc) {

    if (DEBUG && debugflag){
      USLOSS_Console("addProcToReadyList(): Adding process %s to ReadyList\n",
                     proc->name);
    }

    if (ReadyList == NULL) {
        ReadyList = proc; //In this case proc is the sentinel process
    } else {
        // all priorities in list are less than proc
        if(ReadyList->priority > proc->priority) {
            procPtr temp = ReadyList;
            ReadyList = proc;
            proc->nextProcPtr = temp;
        } else { // add proc before first greater priority
            procPtr next = ReadyList->nextProcPtr;
            procPtr last = ReadyList;
            while (next->priority < proc->priority) {
                last = next;
                next = next->nextProcPtr;
            }
            last->nextProcPtr = proc;
        }
    }

    if (DEBUG && debugflag){
      USLOSS_Console("addProcToReadyList(): Process %s added to ReadyList\n",
                     proc->name);
     printReadyList(); 
    }

}

/*---------------------------- printReadyList -----------------------
|  Function printReadyList
|
|  Purpose:  Prints a string representation of the ready list using
|            the USLOSS_Console. Debugging must be enable.
|
|  Parameters:  None
|
|  Returns:  None
*-------------------------------------------------------------------*/
void printReadyList(){
    char str[500];
    procPtr p = ReadyList;
    strcpy(str, p->name);
    while(p->nextProcPtr != NULL){
        p = p->nextProcPtr;
        strcat(str, "->");
        strcat(str, p->name );
    }
    if (DEBUG && debugflag){
        USLOSS_Console("printReadyList(): ReadyList contains: %s\n", str);
    }
}

/*---------------------------- getProcSlot -----------------------
|  Function getProcSlot
|
|  Purpose:  Finds an empty index in the process table (ProcTable). 
|
|  Parameters:  None
|
|  Returns:  -1 if no slot is available or the index of the next
|            empty slot in the process table.
*-------------------------------------------------------------------*/
int getProcSlot() {
    int hashedIndex = currentPID % 50;
    int startIndex = hashedIndex;
    while (ProcTable[hashedIndex].status != EMPTY) {
        hashedIndex++;
        if (hashedIndex > 49) {
            hashedIndex = 0;
        }
        if (hashedIndex == startIndex) {
            return -1;
        }
    }
    return hashedIndex;
}

/*---------------------------- initProcStruct -----------------------
|  Function initProcStruct
|
|  Purpose:  Initializes a ProcStruct. Members are set to 0 or NULL,
|            except in the case of priority which is set to the highest
|            priority of five.
|
|  Parameters:
|      index (IN) --  The index of the ProcStruct in the ProcTable
|
|  Returns:  None
|
|  Side Effects:  The members of the ProcStruct at index are changed.
*-------------------------------------------------------------------*/
void initProcStruct(int index) {

    ProcTable[index].pid = 0;
    ProcTable[index].stackSize = 0;
    ProcTable[index].stack = NULL; 
    ProcTable[index].priority = 5;
    ProcTable[index].status = EMPTY;
    ProcTable[index].childProcPtr = NULL;
    ProcTable[index].nextSiblingPtr = NULL;
    ProcTable[index].nextProcPtr = NULL;
    ProcTable[index].name[0] = '\0';
    ProcTable[index].startArg[0] = '\0';
    ProcTable[index].startFunc = NULL;
    //ProcTable[index].state = NULL;         // TODO:

}
