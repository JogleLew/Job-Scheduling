#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "job.h"
#define DEBUG

int jobid=0;
int siginfo=1;
int fifo;
int globalfd;

struct waitqueue *head=NULL,*head2=NULL,*head3=NULL;
struct jobcmd JGNullcmd;

struct waitqueue *next=NULL,*current =NULL;
int job_identifier=0,prev_identifier=0;
struct timeval interval;
struct itimerval new,old;
struct stat statbuf;
struct sigaction newact,oldact1,oldact2;


void JGsetZero(struct jobcmd *p, int len)
{
	memset(p, 0, len);
}

void JGDebugTask1()
{
	#ifdef DEBUG
		printf("DEBUG IS OPEN!\n");
	#endif
}

void JGDebugTask2()
{
	#ifdef DEBUG
		printf("SIGVTALRM Received!\n");
	#endif	
}

void JGDebugTask3_1()
{
	printf("Reading whether other process send command!\n");
}

void JGDebugTask3_2()
{
	#ifdef DEBUG
		printf("Update jobs in wait queue!\n");
	#endif
}

void JGDebugTask3_3()
{
	#ifdef DEBUG
		printf("Execute enq!\n");
	#endif
}

void JGDebugTask3_4()
{
	#ifdef DEBUG
		printf("Execute deq!\n");
	#endif
}

void JGDebugTask3_5()
{
	#ifdef DEBUG
		printf("Execute stat!\n");
	#endif
}

void JGDebugTask3_6()
{
	#ifdef DEBUG
		printf("Select which job to run next!\n");
	#endif
}

void JGDebugTask3_7()
{
	#ifdef DEBUG
		printf("Switch to the next job!\n");
	#endif
}

void JGDebugTask6_1()
{
	#ifdef DEBUG
		printf("Before update wait queue:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask6_2()
{
	#ifdef DEBUG
		printf("After update wait queue:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask7_1()
{
	#ifdef DEBUG
		printf("Before enq:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask7_2()
{
	#ifdef DEBUG
		printf("After enq:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask7_3()
{
	#ifdef DEBUG
		printf("Before deq:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask7_4()
{
	#ifdef DEBUG
		printf("After deq:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask8(struct waitqueue *select)
{
	char timebuf[BUFLEN];
	#ifdef DEBUG
		printf("Job selected:\n");
		if (select == NULL){
			printf("NULL\n");
			return;
		}
		printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");
		strcpy(timebuf,ctime(&(select->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t\n",
			select->job->jid,
			select->job->pid,
			select->job->ownerid,
			select->job->run_time,
			select->job->wait_time,
			timebuf);
	#endif
}

void JGDebugTask9_1()
{
	#ifdef DEBUG
		printf("Before jobswitch:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask9_2()
{
	#ifdef DEBUG
		printf("After jobswitch:\n");
		do_stat(JGNullcmd);
	#endif
}

void JGDebugTask10()
{
	#ifdef DEBUG
		printf("SIGCHLD Received!\n");
		do_stat(JGNullcmd);
	#endif
}

/* 调度程序 */
void scheduler()
{
	struct jobinfo *newjob=NULL;
	struct jobcmd cmd;
	int  count = 0;
	/*bzero(&cmd,DATALEN);*/ //JG: bzero is an old expression.
	JGsetZero(&cmd,DATALEN); //JG: instead of bzero
	if((count=read(fifo,&cmd,DATALEN))<0)
		error_sys("read fifo failed");
	#ifdef DEBUG
		JGDebugTask3_1();
		if(count){
			printf("cmd cmdtype\t%d\ncmd defpri\t%d\ncmd data\t%s\n",cmd.type,cmd.defpri,cmd.data);
		}
		else
			printf("no data read\n");
	#endif

	/* 更新等待队列中的作业 */
	JGDebugTask3_2();
	updateall();

	switch(cmd.type){
	case ENQ:
		JGDebugTask3_3();
		do_enq(newjob,cmd);
		break;
	case DEQ:
		JGDebugTask3_4();
		do_deq(cmd);
		break;
	case STAT:
		JGDebugTask3_5();
		do_stat(cmd);
		break;
	default:
		break;
	}

	/* 选择高优先级作业 */
	JGDebugTask3_6();
	next=jobselect();
	if (next!=NULL)
		printf("%d\n",next->job->pid);
	/* 作业切换 */
	JGDebugTask3_7();
	jobswitch();
}

int allocjid()
{
	return ++jobid;
}

void updateall()
{
	struct waitqueue *p;
	int tmp;

	/* 更新作业运行时间 */
	if(current)
		if (job_identifier==0)
			current->job->run_time += 1;
		else if (job_identifier==1)
			current->job->run_time += 3;
		else
			current->job->run_time += 5;
	if (job_identifier==0)
		tmp=1000;
	else if (job_identifier==1)
		tmp=3000;
	else
		tmp=5000;	

	JGDebugTask6_1();
	/* 更新作业等待时间及优先级 */
	for(p = head; p != NULL; p = p->next){
		p->job->wait_time += tmp;
		if(p->job->wait_time >= 10000 && p->job->curpri < 3){
			p->job->curpri++;
			p->job->wait_time = 0;
		}
	}
	JGDebugTask6_2();
}

struct waitqueue* jobselect()
{
	struct waitqueue *p,*prev,*select,*selectprev;
	int highest = -1;

	select = NULL;
	selectprev = NULL;
	if(head){
		/* 遍历等待队列中的作业，找到优先级最高的作业 */
		for(prev = head, p = head; p != NULL; prev = p,p = p->next)
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
			printf("found in premium queue\n");
			prev_identifier=job_identifier;
			job_identifier=0;		//job_identifier is zero means that it's found in premium queue
			selectprev->next = select->next;//delete select from the queue
			if (select == selectprev)//the last one deleted from queue, thus end of the queue, no need any modifications
				head = NULL;
	}else if (head2){
		for(prev = head2, p = head2; p != NULL; prev = p,p = p->next)
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
			printf("found in subqueue\n");
			prev_identifier=job_identifier;
			job_identifier=1;
			selectprev->next = select->next;
			if (select == selectprev)
				head2 = NULL;
	}else if (head3){
		for(prev = head3, p = head3; p != NULL; prev = p,p = p->next)
			if(p->job->curpri > highest){
				select = p;
				selectprev = prev;
				highest = p->job->curpri;
			}
			printf("found in minous queue\n");
			prev_identifier=job_identifier;
			job_identifier=2;
			selectprev->next = select->next;
			if (select == selectprev)
				head3 = NULL;
	}
	JGDebugTask8(select);
	return select;
}

void jobswitch()
{
	struct waitqueue *p;
	int i,status;

	/* set up new time interval */
	if (job_identifier==0)
		interval.tv_sec=1;
	else if (job_identifier==1)
		interval.tv_sec=2;
	else
		interval.tv_sec=4;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	JGDebugTask9_1();
	if(current && current->job->state == DONE){ /* 当前作业完成 */
		/* 作业完成，删除它 */
		for(i = 0;(current->job->cmdarg)[i] != NULL; i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i] = NULL;
		}
		/* 释放空间 */
		free(current->job->cmdarg);
		free(current->job);
		free(current);

		current = NULL;
	}

	if(next == NULL && current == NULL){ /* 没有作业要运行 */
		printf("no mission for now\n");
		JGDebugTask9_2();
		return;
	}
	else if (next != NULL && current == NULL){ /* 开始新的作业 */

		printf("begin start new job\n");
		current = next;
		next = NULL;
		sleep(1);
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		kill(current->job->pid,SIGCONT);
		JGDebugTask9_2();
		return;
	}
	else if (next != NULL && current != NULL){ /* 切换作业 */

		printf("switch to Pid: %d\n",next->job->pid);
		kill(current->job->pid,SIGSTOP);
		current->job->curpri = current->job->defpri;
		current->job->wait_time = 0;
		current->job->state = READY;

		/* 放回等待队列 */
		if (prev_identifier==0)
			if(head2){
				for(p = head2; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head2 = current;
			}
		else if (prev_identifier==1)
			if(head3){
				for(p = head3; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head3 = current;
			}
		else
			if(head3){
				for(p = head3; p->next != NULL; p = p->next);
				p->next = current;
			}else{
				head3 = current;
			}
		current = next;
		next = NULL;
		current->job->state = RUNNING;
		current->job->wait_time = 0;
		sleep(1);
		kill(current->job->pid,SIGCONT);
		JGDebugTask9_2();
		return;
	}else{ /* next == NULL且current != NULL，不切换 */
		JGDebugTask9_2();
		return;
	}
}

void sig_handler(int sig,siginfo_t *info,void *notused)
{
	int status;
	int ret;

	switch (sig) {
		case SIGVTALRM: /* 到达计时器所设置的计时间隔 */
			scheduler();
			JGDebugTask2();
			return;
		case SIGCHLD: /* 子进程结束时传送给父进程的信号 */
			ret = waitpid(-1,&status,WNOHANG);
			if (ret == 0)
				return;
			if(WIFEXITED(status)){
				current->job->state = DONE;
				printf("normal termination, exit status = %d\n",WEXITSTATUS(status));
			}else if (WIFSIGNALED(status)){
				printf("abnormal termination, signal number = %d\n",WTERMSIG(status));
			}else if (WIFSTOPPED(status)){
				printf("child stopped, signal number = %d\n",WSTOPSIG(status));
			}
			JGDebugTask10();
			return;
		default:
			return;
	}
}

void do_enq(struct jobinfo *newjob,struct jobcmd enqcmd)
{
	struct waitqueue *newnode,*p;
	int i=0,pid;
	char *offset,*argvec,*q;
	char **arglist;
	sigset_t zeromask;

	sigemptyset(&zeromask);

	/* 封装jobinfo数据结构 */
	newjob = (struct jobinfo *)malloc(sizeof(struct jobinfo));
	newjob->jid = allocjid();
	newjob->defpri = enqcmd.defpri;
	newjob->curpri = enqcmd.defpri;
	newjob->ownerid = enqcmd.owner;
	newjob->state = READY;
	newjob->create_time = time(NULL);
	newjob->wait_time = 0;
	newjob->run_time = 0;
	arglist = (char**)malloc(sizeof(char*)*(enqcmd.argnum+1));
	newjob->cmdarg = arglist;
	offset = enqcmd.data;
	argvec = enqcmd.data;
	while (i < enqcmd.argnum){
		if(*offset == ':'){
			*offset++ = '\0';
			q = (char*)malloc(offset - argvec);
			strcpy(q,argvec);
			arglist[i++] = q;
			argvec = offset;
		}else
			offset++;
	}

	arglist[i] = NULL;

#ifdef DEBUG

	printf("enqcmd argnum %d\n",enqcmd.argnum);
	for(i = 0;i < enqcmd.argnum; i++)
		printf("parse enqcmd:%s\n",arglist[i]);

#endif

	JGDebugTask7_1();
	/*向等待队列中增加新的作业*/
	newnode = (struct waitqueue*)malloc(sizeof(struct waitqueue));
	newnode->next =NULL;
	newnode->job=newjob;

	if(head)
	{
		for(p=head;p->next != NULL; p=p->next);
		p->next =newnode;
	}else{
		printf("head add\n");
		head=newnode;
	}
	JGDebugTask7_2();

	/*为作业创建进程*/
	if((pid=fork())<0)
		error_sys("enq fork failed");

	if(pid==0){
		newjob->pid =getpid();
		/*阻塞子进程,等等执行*/
		raise(SIGSTOP);
#ifdef DEBUG

		printf("begin running\n");
		for(i=0;arglist[i]!=NULL;i++)
			printf("arglist %s\n",arglist[i]);
#endif

		/*复制文件描述符到标准输出*/
		dup2(globalfd,1);
		/* 执行命令 */
		if(execv(arglist[0],arglist)<0)
			printf("exec failed\n");
		exit(1);
	}else{
		newjob->pid=pid;
	}
}

void do_deq(struct jobcmd deqcmd)
{
	int deqid,i;
	struct waitqueue *p,*prev,*select,*selectprev;
	deqid=atoi(deqcmd.data);

	#ifdef DEBUG
		printf("deq jid %d\n",deqid);
	#endif
	JGDebugTask7_3();

	/*current jodid==deqid,终止当前作业*/
	if (current && current->job->jid ==deqid){
		printf("teminate current job\n");
		kill(current->job->pid,SIGKILL);
		for(i=0;(current->job->cmdarg)[i]!=NULL;i++){
			free((current->job->cmdarg)[i]);
			(current->job->cmdarg)[i]=NULL;
		}
		free(current->job->cmdarg);
		free(current->job);
		free(current);
		current=NULL;
	}
	else{ /* 或者在等待队列中查找deqid */
		select=NULL;
		selectprev=NULL;
		if(head){
			for(prev=head,p=head;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head=NULL;
		}
		if(head2){
			for(prev=head2,p=head2;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head2=NULL;
		}
		if(head3){
			for(prev=head3,p=head3;p!=NULL;prev=p,p=p->next)
				if(p->job->jid==deqid){
					select=p;
					selectprev=prev;
					break;
				}
				selectprev->next=select->next;
				if(select==selectprev)
					head3=NULL;
		}
		if(select){
			for(i=0;(select->job->cmdarg)[i]!=NULL;i++){
				free((select->job->cmdarg)[i]);
				(select->job->cmdarg)[i]=NULL;
			}
			free(select->job->cmdarg);
			free(select->job);
			free(select);
			select=NULL;
		}
	}
	JGDebugTask7_4();
}

void do_stat(struct jobcmd statcmd)
{
	struct waitqueue *p;
	char timebuf[BUFLEN];
	/*
	*打印所有作业的统计信息:
	*1.作业ID
	*2.进程ID
	*3.作业所有者
	*4.作业运行时间
	*5.作业等待时间
	*6.作业创建时间
	*7.作业状态
	*/

	/* 打印信息头部 */
	printf("JOBID\tPID\tOWNER\tRUNTIME\tWAITTIME\tCREATTIME\t\tSTATE\n");	//current job
	if(current){
		strcpy(timebuf,ctime(&(current->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			current->job->jid,
			current->job->pid,
			current->job->ownerid,
			current->job->run_time,
			current->job->wait_time,
			timebuf,"RUNNING");
	}
	
	printf("jobs in premium queue:\n");
	for(p=head;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}

	printf("jobs in subordinate queue:\n");
	for(p=head2;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}

	printf("jobs in minous queue:\n");
	for(p=head3;p!=NULL;p=p->next){
		strcpy(timebuf,ctime(&(p->job->create_time)));
		timebuf[strlen(timebuf)-1]='\0';
		printf("%d\t%d\t%d\t%d\t%d\t%s\t%s\n",
			p->job->jid,
			p->job->pid,
			p->job->ownerid,
			p->job->run_time,
			p->job->wait_time,
			timebuf,
			"READY");
	}
}

int main()
{
	struct timeval interval;
	struct itimerval new,old;
	struct stat statbuf;
	struct sigaction newact,oldact1,oldact2;

	JGDebugTask1();
	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0)
			error_sys("remove failed");
	}

	if(mkfifo("/tmp/server",0666)<0) //0666 means -rw-rw-rw-
		error_sys("mkfifo failed");
	/* 在非阻塞模式下打开FIFO */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0)
		error_sys("open fifo failed");

	/* 建立信号处理函数 */
	newact.sa_sigaction=sig_handler;
	sigemptyset(&newact.sa_mask);
	newact.sa_flags=SA_SIGINFO;
	sigaction(SIGCHLD,&newact,&oldact1);
	sigaction(SIGVTALRM,&newact,&oldact2);

	/* 设置时间间隔为1000毫秒 */
	interval.tv_sec=1;
	interval.tv_usec=0;

	new.it_interval=interval;
	new.it_value=interval;
	setitimer(ITIMER_VIRTUAL,&new,&old);

	while(siginfo==1);

	close(fifo);
	close(globalfd);
	return 0;
}
