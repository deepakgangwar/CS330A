// exception.cc
//  Entry point into the Nachos kernel from user programs.
//  There are two kinds of things that can cause control to
//  transfer back to here from user code:
//
//  syscall -- The user code explicitly requests to call a procedure
//  in the Nachos kernel.  Right now, the only function we support is
//  "Halt".
//
//  exceptions -- The user code does something that the CPU can't handle.
//  For instance, accessing memory that doesn't exist, arithmetic errors,
//  etc.
//
//  Interrupts (which can also cause control to transfer from user
//  code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"
#include "console.h"
#include "synch.h"

//----------------------------------------------------------------------
// ExceptionHandler
//  Entry point into the Nachos kernel.  Called when a user program
//  is executing, and either does a syscall, or generates an addressing
//  or arithmetic exception.
//
//  For system calls, the following is the calling convention:
//
//  system call code -- r2
//      arg1 -- r4
//      arg2 -- r5
//      arg3 -- r6
//      arg4 -- r7
//
//  The result of the system call, if any, must be put back into r2.
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//  "which" is the kind of exception.  The list of possible exceptions
//  are in machine.h.
//----------------------------------------------------------------------
static Semaphore *readAvail;
static Semaphore *writeDone;
static void ReadAvail(int arg) { readAvail->V(); }
static void WriteDone(int arg) { writeDone->V(); }

static void ConvertIntToHex (unsigned v, Console *console)
{
   unsigned x;
   if (v == 0) return;
   ConvertIntToHex (v/16, console);
   x = v % 16;
   if (x < 10) {
      writeDone->P() ;
      console->PutChar('0'+x);
   }
   else {
      writeDone->P() ;
      console->PutChar('a'+x-10);
   }
}

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);
    int memval, vaddr, printval, tempval, exp;
    unsigned printvalus;        // Used for printing in hex
    if (!initializedConsoleSemaphores) {
       readAvail = new Semaphore("read avail", 0);
       writeDone = new Semaphore("write done", 1);
       initializedConsoleSemaphores = true;
    }
    Console *console = new Console(NULL, NULL, ReadAvail, WriteDone, 0);;

    if ((which == SyscallException) && (type == SysCall_Halt)) {
    DEBUG('a', "Shutdown, initiated by user program.\n");
    interrupt->Halt();
    }
    else if ((which == SyscallException) && (type == SysCall_PrintInt)) {
       printval = machine->ReadRegister(4);
       if (printval == 0) {
      writeDone->P() ;
          console->PutChar('0');
       }
       else {
          if (printval < 0) {
         writeDone->P() ;
             console->PutChar('-');
             printval = -printval;
          }
          tempval = printval;
          exp=1;
          while (tempval != 0) {
             tempval = tempval/10;
             exp = exp*10;
          }
          exp = exp/10;
          while (exp > 0) {
         writeDone->P() ;
             console->PutChar('0'+(printval/exp));
             printval = printval % exp;
             exp = exp/10;
          }
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintChar)) {
    writeDone->P() ;
        console->PutChar(machine->ReadRegister(4));   // echo it!
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintString)) {
       vaddr = machine->ReadRegister(4);
       machine->ReadMem(vaddr, 1, &memval);
       while ((*(char*)&memval) != '\0') {
      writeDone->P() ;
          console->PutChar(*(char*)&memval);
          vaddr++;
          machine->ReadMem(vaddr, 1, &memval);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_PrintIntHex)) {
       printvalus = (unsigned)machine->ReadRegister(4);
       writeDone->P() ;
       console->PutChar('0');
       writeDone->P() ;
       console->PutChar('x');
       if (printvalus == 0) {
          writeDone->P() ;
          console->PutChar('0');
       }
       else {
          ConvertIntToHex (printvalus, console);
       }
       // Advance program counters.
       machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
       machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
       machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_GetReg)){
        int reg_no = machine->ReadRegister(4); // register number to read will be in $4
        machine->WriteRegister(2,machine->ReadRegister(reg_no)); // syscall returns to $2

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_GetPA)) {
        int virtAddr = machine->ReadRegister(4); // argument passed
        int physAddr, vpn, offset, pageFrame;

        //based on machine/translate.cc
        vpn = (unsigned) virtAddr / PageSize;
        offset = (unsigned) virtAddr % PageSize;
        pageFrame = (&(machine->KernelPageTable[vpn]))->physicalPage;
        physAddr = pageFrame * PageSize + offset;

        if ((vpn >= machine->pageTableSize) || (!machine->KernelPageTable[vpn].valid) || (pageFrame >= NumPhysPages)) machine->WriteRegister(2,-1);
        else machine->WriteRegister(2,physAddr);

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_GetPID)) {
        machine->WriteRegister(2,currentThread->getPID());

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_GetPPID)) {
        machine->WriteRegister(2,currentThread->getPPID());

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_Time)) {
        machine->WriteRegister(2,stats->totalTicks);

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_Yield)) {
        currentThread->YieldCPU();

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_NumInstr)) {
        machine->WriteRegister(2,currentThread->NumInstr);

        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if ((which == SyscallException) && (type == SysCall_Exec)) {
        vaddr = machine->ReadRegister(4);
        machine->ReadMem(vaddr, 1, &memval);
        char file_path[1000];
        int i=0;
        while ((*(char*)&memval) != '\0') {
            file_path[i] = *(char*)&memval;
            i++;
            vaddr++;
            machine->ReadMem(vaddr, 1, &memval);
        }
        file_path[i] = '\0';
        OpenFile *executable = fileSystem->Open(file_path);

        if (executable == NULL) {
    	       ASSERT(FALSE);
        }
        currentThread->space = new ProcessAddressSpace(executable);

        delete executable;			// close file

        currentThread->space->InitUserModeCPURegisters();		// set the initial register values
        currentThread->space->RestoreContextOnSwitch();		// load page table register

        machine->Run();			// jump to the user progam
        ASSERT(FALSE);			// machine->Run never returns;
    					// the address space exits
    					// by doing the syscall "exit"
    }
    else if ((which == SyscallException) && (type == SysCall_Sleep)) {
        int stime = machine->ReadRegister(4); // argument passed
        if(stime==0)
            currentThread->YieldCPU();
        else{
            IntStatus temp = interrupt->SetLevel(IntOff);
            Waitlist->SortedInsert((void*)currentThread, stats->totalTicks+stime);
            currentThread->PutThreadToSleep();
            (void) interrupt->SetLevel(temp);
        }
        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else if((which == SyscallException) && (type == SysCall_Exit)) {
	// One case to be handled: when a child thread terminates, we must send a signal to
	// the parent thread in case it called wait(NULL) in the program waiting for the child
	// to terminate.
        int curPid = currentThread->getPID();
        NachOSThread* parThread=currentThread->parentThread;
        // If parent is waiting for this particular child
        if(parThread != NULL ){
            if(parThread->joinpid == curPid){
                IntStatus oldLevel = interrupt->SetLevel(IntOff);
                scheduler->MoveThreadToReadyQueue(parThread); // MoveThreadToReadyQueue assumes that interrupts are disabled!
                (void) interrupt->SetLevel(oldLevel);
            }
            parThread->SetExitCode(curPid, machine->ReadRegister(4));
            DEBUG('t',"setting exit code of thread \"%d\" with \" %d\" \n",curPid,machine->ReadRegister(4));
        }
        if(NachOSThread::NumOfThreads == 1){
           interrupt->Halt();
        }
        else{
             currentThread->FinishThread();
        }
    }
    else if((which == SyscallException) && (type == SysCall_Fork)){
	    //child naming still left!
		NachOSThread* childThread = new NachOSThread("child");
        // updating parent thread
        childThread->parentThread=currentThread;
        currentThread->SetChild(childThread->getPID());
		//	
		//make a new address space entry 
		childThread->space= new ProcessAddressSpace(currentThread->space->getNumVirtualPages(),currentThread->space->ProcessStartPage);
		//set up the page table
		// copy the contents of the parent space

		//  CreateThreadStack()
		// func that does -  threads needed to be destroyed are destroyed, and the registers and the address space of the scheduled thread are restored
		// func also - call run 
        childThread->ThreadFork(func,0);
		// place child pid in return of parent
        machine->WriteRegister(2,childThread->getPID());
        // Advance program counters.
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
        //save all registers for child
        childThread->SaveUserState(); 
        //set return value zero for child
        childThread->setUserRegisters(2,0);
    }
    else if((which == SyscallException) && (type == SysCall_Join))
    {
        int pid = machine->ReadRegister(4);
        if(!currentThread->FindChild(pid)){
            machine->WriteRegister(2,-1);
            DEBUG('t', "Thread with pid \"%d\" is not child of the thread \"%d\" \n", pid, currentThread->getPID());
        }
        else{
            int exitcode = currentThread->GetExitCode(pid);
            DEBUG('t', "Exitcode of child \"%d\" is \"%d\" \n",pid,exitcode);
            if(exitcode!=-1){
                machine->WriteRegister(2,exitcode);
            }
            else{
                IntStatus temp = interrupt->SetLevel(IntOff);
                currentThread->joinpid = pid;
                currentThread->PutThreadToSleep();
                (void) interrupt->SetLevel(temp);
            }
        }
       
        machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));
        machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));
        machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg)+4);
    }
    else{
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }
}
