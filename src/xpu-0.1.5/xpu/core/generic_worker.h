/**
 * @file    generic_worker.h
 * @author  Nader KHAMMASSI
 * @contact nader.khammassi@gmail.com
 * @brief   asynchronous generic worker
 *
 * @copyright
 *
 * XPU - C++ Parallel  Programming Library for Multicore Architectures
 *
 * Copyright (C) 2014 Nader Khammassi, All Rights Reserved.
 *
 * This file is part of XPU and has been downloaded from 
 * http://www.xpu-project.net/.
 *
 * XPU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * XPU is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __XPU_GENERIC_WORKER_H__
#define __XPU_GENERIC_WORKER_H__

#include <xpu/core/os/thread.h>
#include <xpu/core/os/control.h>

#include <xpu/core/work.h>
#include <xpu/core/queue.h>

using namespace xpu::core::os;

namespace xpu
{
   namespace core
   {
	 class work_queue
	 {
	    public:
		 
		 work_queue()
		 {
		    m_control = new data_control;
		    m_queue   = new queue<work*>;
		 }
		 
		 ~work_queue()
		 {
		    delete m_control;
		    delete m_queue;
		 }
		 
		 inline data_control * get_control()
		 {
		    return m_control;
		 }

		 inline queue<work *> * get_queue()
		 {
		    return m_queue;
		 }
		 
		 inline void deactivate()
		 {
		    m_control->deactivate();
		 }

		 inline void push(work * w)
		 {
		    m_control->lock();
		    m_queue->push(w);
		    m_control->unlock();
		    m_control->broadcast();
		 }

		 inline bool empty()
		 {
		    return (m_queue->size() == 0);
		 }

		 inline int size()
		 {
		    return m_queue->size();
		 }
	    
		private:
		  
		 data_control  * m_control;
		 queue<work *> * m_queue;
		  

	 };

	 /**
	  * generic worker
	  */
	 class generic_worker : public thread
	 {
	    public:

		  generic_worker(int cpu, work_queue * wq) : thread(NULL), m_cpu(cpu), m_work_queue(wq)
	    {
		  //m_cpuset.only(cpu);
		  //m_control = new data_control;
		  //m_queue   = new queue<work*>;
	    }

		  ~generic_worker()
		  {
			//delete m_control;
			//delete m_queue;
		  }

		  void stop()
		  {
			//m_control->deactivate();
			m_work_queue->deactivate();
		  }

		  void submit(work * w)
		  {
			m_work_queue->push(w);
		  }

		  int get_cpu()
		  {
			return m_cpu;
		  }

		  void set_cpu(int cpu)
		  {
			m_cpu = cpu;
			m_cpuset.only(cpu);
		  }

		  void start()
		  {
			pthread_create(&m_id, &m_attr, reinterpret_cast<__xpu_task>(&run), (void*)this); 
		  }

	    protected:

		  int             m_cpu;
		  work_queue *    m_work_queue;

		  static
			void * run(void * args)
			{
			   //println("[+] generic worker started (tid:" << pthread_self() << ")");
			   generic_worker * t = (generic_worker*)args;
			   t->m_cpuset.setup();
			   data_control * t_control = t->m_work_queue->get_control();
			   queue<work*> * t_queue   = t->m_work_queue->get_queue();
			   t_control->lock();
			   while (t_control->active())
			   {
				 while (t_queue->empty() && t_control->active()) 
				 {
				    t_control->wait();
				 }
				 if (!t_control->active())
				 {
				    t_control->unlock();
				    //println("[+] generic worker left : work queue not active anymore (tid:" << pthread_self() << ")");
				    break;
				 }
				 try 
				 {
				    work * w = t_queue->pop();
				    t_control->unlock();
				    w->perform();
				    t_control->lock();
				 } catch (exception e) { /* println("[" << pthread_self() << "] work missed."); */ }
			   }
			   t_control->signal();
			   t_control->unlock();
			   //println("[+] generic worker stopped (tid:" << pthread_self() << ")");
			   return NULL;
			}
	 };
   } // core
} // xpu


#endif // __XPU_GENERIC_WORKER_H__
