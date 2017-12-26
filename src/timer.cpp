/* -----------------------------------------------------------------------------

  BrainBay  -  Version 2.0, GPL 2003-2017

  MODULE:  TIMER.CPP
  Author:  Chris Veigl


  This Module provides the basic timing-functions for sample-processing
  The TimerProc() - function performs reading from COM-ports or archives and
  triggers the worker-functions of all objects in case of an archive read.
  The Windows PerformanceCounter-functions are used to retrieve excat system time

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; See the
  GNU General Public License for more details.

  
--------------------------------------------------------------------------------*/


#include "brainBay.h"
#include "neurobit_api\\api.h"
#include "ob_emotiv.h"
#include <set>
#include <map>
#include <vector>

static std::vector<BASE_CL*> sorted_objects;

/**
a is subset of b?
*/
bool is_subset(std::set<BASE_CL*>& a, std::set<BASE_CL*>& b) {
	for (auto i = a.begin(); i != a.end(); i++) {
		if (b.find(*i) == b.end()) {
			return false;
		}
	}
	return true;
}

void dep_resolve() {
	// first we have to find nodes without parent, this are the first resolved nodes
	std::set<BASE_CL*> all;

	std::map<BASE_CL*, std::set<BASE_CL*>> back;

	// copy all nodes to set all and back is filled with ancestors
	for (int t = 0; t < GLOBAL.objects; t++)  {
		BASE_CL* o = objects[t];
		if (o) {
			all.insert(o);
			std::set<BASE_CL*> x;
			for (LINKStruct* act_link=o->out; act_link->to_port!=-1;act_link++) {			
				x.insert(objects[act_link->to_object]);
			}
			back[o] =  x;
		}
	}

	std::set<BASE_CL*> s_unresolved;
	// delete all delependant child objects and insert them into s_unresolved
	for (int t=0; t<GLOBAL.objects; t++)  {
	 	 BASE_CL* o = objects[t];
		 if (o) {			  
			for (LINKStruct* act_link=o->out; act_link->to_port!=-1;act_link++) {			
				BASE_CL* child = objects[act_link->to_object];
				all.erase(child);
				s_unresolved.insert(child);
			}
		 }
	}

	std::vector<BASE_CL*> resolved(all.begin(), all.end());
	std::set<BASE_CL*> s_resolved(all);

	// brute force
	int lastcnt = s_unresolved.size() + 1;
	while (lastcnt > s_unresolved.size() && s_unresolved.size() > 0) {
		lastcnt = s_unresolved.size();
		for (auto i = s_unresolved.begin(); i!= s_unresolved.end(); ) { // increment in the body, because of erasing
			if (is_subset(back.find(*i)->second, s_resolved)) {
				BASE_CL* found = *i++;
				s_unresolved.erase(found);
				s_resolved.insert(found);
				resolved.push_back(found);
			}
			else {
				i++;
			}
		}
	}

	sorted_objects.clear();
	for (auto i = resolved.begin(); i !=  resolved.end(); i++) {
		sorted_objects.push_back(*i);
	}
	if (s_unresolved.size()) {
		// an error.
		for (auto i = s_unresolved.begin(); i !=  s_unresolved.end(); i++) {
			sorted_objects.push_back(*i);
		}
	}
	if (GLOBAL.objects != sorted_objects.size()) {
		MessageBox(0, "aaaa", "aaa", MB_OK);
	}
}

extern TProtocolEngine NdProtocolEngine;


void init_system_time(void)
{
	QueryPerformanceFrequency((_LARGE_INTEGER *)&(TIMING.pcfreq));

    TTY.packettime=(LONGLONG)(TIMING.pcfreq/PACKETSPERSECOND); // milliseconds for one Packet-Read

	TIMING.ppscounter=0;
	TIMING.packetcounter=0;
	TIMING.dialog_update=0;
	TIMING.draw_update=0;

}



void process_packets(void)
{
	static double calc;
	int t;

    TIMING.ppscounter++;
    TIMING.packetcounter++;
	

	if (TIMING.dialog_update++ >= GLOBAL.dialog_interval) TIMING.dialog_update=0; 
	if (TIMING.draw_update++ >= GLOBAL.draw_interval) TIMING.draw_update=0; 


	if (GLOBAL.session_length && (TIMING.packetcounter>=GLOBAL.session_end))
	{
		int sav_fly=GLOBAL.fly;
		SendMessage(ghWndStatusbox,WM_COMMAND,IDC_STOPSESSION,0);
		
		TIMING.packetcounter= GLOBAL.session_start;
		for (t=0;t<GLOBAL.objects;t++) objects[t]->session_pos(TIMING.packetcounter);
		int pos=get_sliderpos(TIMING.packetcounter);
		SendMessage(GetDlgItem(ghWndStatusbox,IDC_SESSIONPOS),TBM_SETSELSTART,TRUE,pos);

		if ((!sav_fly)&&(GLOBAL.session_loop) )
		{
				reset_oscilloscopes();
				SendMessage(ghWndStatusbox,WM_COMMAND,IDC_RUNSESSION,0);
		}
		 		
	}
	else
	{
		for (auto i  = sorted_objects.begin(); i != sorted_objects.end(); i++) {
			(*i)->work();
		}
	}
	if (!TIMING.dialog_update) update_statusinfo();
	
}
	

void CALLBACK TimerProc(UINT uID,UINT uMsg,DWORD dwUser,DWORD dw1,DWORD dw2)
{
	LONGLONG pc;
	MSG msg;

    QueryPerformanceCounter((_LARGE_INTEGER *)&pc);
	//TIMING.acttime=pc;

    check_keys();
    if (GLOBAL.neurobit_available) NdProtocolEngine();
	if (GLOBAL.emotiv_available) process_emotiv();
//	if (GLOBAL.ganglion_available) process_ganglion();

	if ((!TIMING.pause_timer) && (!GLOBAL.loading))
	{
		// one second passed ? -> update PPS-info
		if (pc-TIMING.timestamp >= TIMING.pcfreq) 
		{	TIMING.timestamp+=TIMING.pcfreq; 
			TIMING.actpps=(int) TIMING.ppscounter;
		    TIMING.ppscounter=0;
		}

		// Reading from Archive & next packet demanded? -> read from File and Process Packets
		while ((pc-TIMING.readtimestamp >= TTY.packettime) || (GLOBAL.fly))
		{
			TIMING.readtimestamp+=TTY.packettime;
			TIMING.acttime=TIMING.readtimestamp;

			if(CAPTFILE.do_read&&(CAPTFILE.offset<=TIMING.packetcounter)&&(CAPTFILE.offset+CAPTFILE.length>TIMING.packetcounter))
			{
				long tmp;
				tmp=TIMING.packetcounter;
				read_captfile(TTY.amount_to_read); 	
				ParseLocalInput(TTY.amount_to_read);
				if ((TIMING.packetcounter-tmp-1)>0)
				 TIMING.readtimestamp+=TTY.packettime*(TIMING.packetcounter-tmp-1);
				//return;
			}

			// process packets in case of no File-Read and no Com-Read
			else if ((TTY.read_pause) && (!GLOBAL.neurobit_available) && (!GLOBAL.emotiv_available) && (!GLOBAL.ganglion_available))  
				process_packets();

			if (GLOBAL.fly)
			{
				if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
				{	
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		}	
	}
}


void start_timer(void)
{
	if (TIMING.timerid) stop_timer();

	dep_resolve();
	/*sorted_objects.clear();
	for (int t = 0; t < GLOBAL.objects; t++)  {
		BASE_CL* o = objects[t];
		sorted_objects.push_back(o);
	}	
	*/
	QueryPerformanceCounter((_LARGE_INTEGER *)&TIMING.timestamp);
	TIMING.readtimestamp=TIMING.timestamp;
	TIMING.ppscounter=0;

	if(!(TIMING.timerid= timeSetEvent(1,0,TimerProc,1,TIME_PERIODIC | TIME_CALLBACK_FUNCTION)))
							report_error("Could not set Timer!");
	else GLOBAL.running=TRUE; 
}


void stop_timer(void)
{
	if(TIMING.timerid)
	{
	  if(timeKillEvent(TIMING.timerid)!=TIMERR_NOERROR)
		report_error("Could not stop Timer!");
	  else TIMING.timerid=0;
	}

	GLOBAL.running=FALSE;
	mute_all_midi();

}


