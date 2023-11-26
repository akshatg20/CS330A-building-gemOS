#include<context.h>
#include<memory.h>
#include<lib.h>
#include<entry.h>
#include<file.h>
#include<tracer.h>


///////////////////////////////////////////////////////////////////////////
//// 		Start of Trace buffer functionality 		      /////
///////////////////////////////////////////////////////////////////////////

// checks if memory passed from user buffer is valid
int is_valid_mem_range(unsigned long buff, u32 count, int access_bit)
{
	int flag = 0;
	struct mm_segment mm_segment;
	struct vm_area *vm_area;
	struct exec_context *exec_context = get_current_ctx();

	// checking if buffer lies in mm_segment
	for (int i = 0; i < MAX_MM_SEGS; i++)
	{
		mm_segment = exec_context->mms[i];
		if ((mm_segment.start <= buff) && (mm_segment.end > buff + count - 1))
		{
			flag = mm_segment.access_flags;
			break;
		}
	}

	// checking if buffer lies in vm_area
	if (flag == 0)
	{
		vm_area = exec_context->vm_area;
		while (vm_area != NULL)
		{
			if ((vm_area->vm_start <= buff) && (vm_area->vm_end > buff + count - 1))
			{
				flag = vm_area->access_flags;
				break;
			}
			vm_area = vm_area->vm_next;
		}
	}

	// checking proper mode and valid address
	if (flag & access_bit)
		return 1;
	return 0;
}

// syscall to close the trace buffer
long trace_buffer_close(struct file *filep)
{
	// in case of an empty file pointer
	if (filep == NULL)
		return -EINVAL;

	// deallocate the 4KB memory for the trace buffer
	os_page_free(USER_REG, filep->trace_buffer->buffer);

	// deallocate the fileops, trace_buffer_info and file objects
	os_free(filep->fops, sizeof(struct fileops));
	os_free(filep->trace_buffer, sizeof(struct trace_buffer_info));
	os_free(filep, sizeof(struct file));

	return 0;
}

// syscall to read from the trace buffer
int trace_buffer_read(struct file *filep, char *buff, u32 count)
{
	// in case there is no trace buffer
	if (filep == NULL) {
		return -EINVAL;
	}

	if(count < 0) {
		return -EINVAL;
	}

	// in case of invalid buffer
	if (is_valid_mem_range((unsigned long)buff, count, 2) == 0) {
		return -EBADMEM;
	}

	// in case trace buffer doesn't have read access
	if (filep->mode & O_READ == 0)
	{
		// printk("wrong access\n");
		return -EINVAL;
	}

	u32 numBytesRead = 0;

	// define the front and rear of the trace buffer queue
	int front = filep->trace_buffer->write_offset;
	int rear = filep->trace_buffer->read_offset;

	// in case the trace buffer is empty
	if (front == -1 && rear == -1)
		return 0;

	while (numBytesRead < count)
	{

		buff[numBytesRead] = filep->trace_buffer->buffer[rear];
		numBytesRead = numBytesRead + 1;

		// nothing more to read
		if (rear == front)
		{
			filep->trace_buffer->write_offset = -1;
			filep->trace_buffer->read_offset = -1;
			front = rear = -1;
			break;
		}

		rear = (rear + 1) % TRACE_BUFFER_MAX_SIZE;
	}

	// update the read offset
	filep->trace_buffer->read_offset = rear;

	return numBytesRead;
}

// syscall to write to the trace buffer
int trace_buffer_write(struct file *filep, char *buff, u32 count)
{
	// in case there is no trace buffer
	if (filep == NULL) {
		return -EINVAL;
	}

	// if count is negative
	if(count < 0) {
		return -EINVAL;
	}

	// in case of invalid buffer
	if (is_valid_mem_range((unsigned long)buff, count, 1) == 0) {
		return -EBADMEM;
	}

	// in case trace buffer doesn't have write access
	if (filep->mode & O_WRITE == 0)
	{
		// printk("error arising from wrong access\n");
		return -EINVAL;
	}

	u32 numBytesWritten = 0;

	// define the front and rear of the trace buffer queue
	int front = filep->trace_buffer->write_offset;
	int rear = filep->trace_buffer->read_offset;

	// in case the trace buffer is full
	if ((front + 1) % TRACE_BUFFER_MAX_SIZE == rear)
		return 0;

	while (numBytesWritten < count)
	{

		// trace buffer is empty, initialize it
		if (front == -1 && rear == -1)
		{
			filep->trace_buffer->write_offset = 0;
			filep->trace_buffer->read_offset = 0;
			front = rear = 0;
		}
		// writing around the queue
		else
		{
			front = (front + 1) % TRACE_BUFFER_MAX_SIZE;
		}

		filep->trace_buffer->buffer[front] = buff[numBytesWritten];
		numBytesWritten = numBytesWritten + 1;

		// trace buffer is full
		if ((front + 1) % TRACE_BUFFER_MAX_SIZE == rear)
			break;
	}

	// update the write offset
	filep->trace_buffer->write_offset = front;

	return numBytesWritten;
}

// syscall to create a trace buffer
int sys_create_trace_buffer(struct exec_context *current, int mode)
{
	int free_fd = -1;
	int i;

	if (current == NULL) {
		return -EINVAL;
	}

	// allocate the lowest free file descriptor
	for (i = 0; i < MAX_OPEN_FILES; i++)
	{
		if (current->files[i] == NULL)
		{
			free_fd = i;
			break;
		}
	}

	if((mode != O_READ && mode != O_WRITE && mode != O_RDWR) || (free_fd == -1)) {
		return -EINVAL;
	}

	// allocate a file pointer and initialize its fields
	struct file *fp = os_alloc(sizeof(struct file));

	if(fp == NULL) {
		return -ENOMEM;
	}

	fp->type = TRACE_BUFFER;
	fp->mode = mode;
	fp->inode = NULL;
	fp->offp = 0;
	fp->ref_count = 1;

	// allocate trace buffer object
	struct trace_buffer_info *tracer = os_alloc(sizeof(struct trace_buffer_info));

	if(tracer == NULL) {
		return -ENOMEM;
	}

	tracer->buffer = (char *)os_page_alloc(USER_REG);

	if(tracer->buffer == NULL) {
		return -ENOMEM;
	}

	tracer->write_offset = tracer->read_offset = -1;
	fp->trace_buffer = tracer;

	// allocate file pointers object
	struct fileops *filePointers = os_alloc(sizeof(struct fileops));

	if(filePointers == NULL) {
		return -ENOMEM;
	}

	filePointers->read = trace_buffer_read;
	filePointers->write = trace_buffer_write;
	filePointers->close = trace_buffer_close;
	fp->fops = filePointers;

	// assigning the file to the current process
	current->files[free_fd] = fp;

	return free_fd;
}

///////////////////////////////////////////////////////////////////////////
//// 		Start of strace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

// syscall to write to the trace buffer from OS
int write_to_buffer(struct file *filep, char *buff, u32 count)
{
	// in case there is no trace buffer
	if (filep == NULL)
	{
		return -EINVAL;
	}

	u32 numBytesWritten = 0;

	// define the front and rear of the trace buffer queue
	int front = filep->trace_buffer->write_offset;
	int rear = filep->trace_buffer->read_offset;

	// in case the trace buffer is full
	if ((front + 1) % TRACE_BUFFER_MAX_SIZE == rear)
		return 0;

	while (numBytesWritten < count)
	{
		// trace buffer is empty, initialize it
		if (front == -1 && rear == -1)
		{
			filep->trace_buffer->write_offset = 0;
			filep->trace_buffer->read_offset = 0;
			front = rear = 0;
		}
		// writing around the queue
		else
		{
			front = (front + 1) % TRACE_BUFFER_MAX_SIZE;
		}

		filep->trace_buffer->buffer[front] = buff[numBytesWritten];
		numBytesWritten = numBytesWritten + 1;

		// trace buffer is full
		if ((front + 1) % TRACE_BUFFER_MAX_SIZE == rear)
			break;
	}

	// update the write offset
	filep->trace_buffer->write_offset = front;

	return numBytesWritten;
}

// syscall to store tracing information of the system calls
int perform_tracing(u64 syscall_num, u64 param1, u64 param2, u64 param3, u64 param4)
{
	// ignore invocations of perform tracing by start_strace, end_trace
	if (syscall_num == 37 || syscall_num == 38)
		return 0;

	struct exec_context *current = get_current_ctx();

	// if strace list hasn't been created yet, return
	if (current->st_md_base == NULL)
		return 0;

	// if tracing is off, do not write to the trace buffer
	if (current->st_md_base->is_traced == 0)
		return 0;

	// if perform tracing has been called by forked process, return
	if (current->pid != current->st_md_base->is_traced) {
		return 0;
	}

	// get the file descriptor for the trace buffer
	int fd = current->st_md_base->strace_fd;

	// in case of filtered_tracing, check if the system call being passed should be traced or not
	if (current->st_md_base->tracing_mode == FILTERED_TRACING)
	{
		struct strace_info *temp = current->st_md_base->next;
		int found = 0;
		while (temp != NULL)
		{
			if (temp->syscall_num == syscall_num)
			{
				found = 1;
				break;
			}
			temp = temp->next;
		}

		// if system call not found, do not write to the trace buffer
		if (found == 0)
			return 0;
	}

	// write the syscall_num to the buffer
	write_to_buffer(current->files[fd], (char *)&syscall_num,8);

	int sr = syscall_num;
	int numBytesWritten = 8;

	// check the number of arguments passed

	// Case I : Zero arguments
	if (sr == 2 || sr == 10 || sr == 11 || sr == 13 || sr == 15 || sr == 20 || sr == 21 || sr == 22 || sr == 38)
	{
		numBytesWritten = 8;
	}

	// Case II: One argument
	else if (sr == 1 || sr == 7 || sr == 14 || sr == 19 || sr == 27 || sr == 29 || sr == 36)
	{
		write_to_buffer(current->files[fd], (char *)&param1,8);
		numBytesWritten = 16;
	}

	// Case III: Two arguments
	else if (sr == 4 || sr == 8 || sr == 9 || sr == 12 || sr == 17 || sr == 23 || sr == 28 || sr == 37 || sr == 40)
	{
		write_to_buffer(current->files[fd], (char *)&param1,8);
		write_to_buffer(current->files[fd], (char *)&param2,8);
		numBytesWritten = 24;
	}

	// Case IV: Three arguments
	else if (sr == 18 || sr == 24 || sr == 25 || sr == 30 || sr == 39 || sr == 41)
	{
		write_to_buffer(current->files[fd], (char *)&param1,8);
		write_to_buffer(current->files[fd], (char *)&param2,8);
		write_to_buffer(current->files[fd], (char *)&param3,8);
		numBytesWritten = 32;
	}

	// Case V: Four arguments
	else if (sr == 16 || sr == 35)
	{
		write_to_buffer(current->files[fd], (char *)&param1,8);
		write_to_buffer(current->files[fd], (char *)&param2,8);
		write_to_buffer(current->files[fd], (char *)&param3,8);
		write_to_buffer(current->files[fd], (char *)&param4,8);
		numBytesWritten = 40;
	}

	// add a delimiter to the trace buffer
	char* delimiter = (char*)(' ');
	write_to_buffer(current->files[fd],(char*)&delimiter,1);

	return 0;
}

// syscall to modify tracing information for particular syscalls
int sys_strace(struct exec_context *current, int syscall_num, int action)
{
	// allocate and initialize the list containing info about the syscalls being traced
	if (current->st_md_base == NULL)
	{
		struct strace_head *listHead = os_alloc(sizeof(struct strace_head));
		listHead->count = 0;
		listHead->next = listHead->last = NULL;
		listHead->is_traced = 0;
		current->st_md_base = listHead;
	}

	if (action == ADD_STRACE)
	{

		// return error if max limit already reached
		if (current->st_md_base->count == STRACE_MAX)
		{
			return -EINVAL;
		}

		// check if the syscall being added is already present in the list or not
		struct strace_info *temp = current->st_md_base->next;
		while (temp != NULL)
		{
			if (temp->syscall_num == syscall_num)
			{
				return -EINVAL;
			}
			temp = temp->next;
		}

		// create the list node
		struct strace_info *node = os_alloc(sizeof(struct strace_info));
		node->syscall_num = syscall_num;
		node->next = NULL;

		// add the syscall to the list
		if (current->st_md_base->count == 0)
		{
			current->st_md_base->next = current->st_md_base->last = node;
		}
		else
		{
			current->st_md_base->last->next = node;
			current->st_md_base->last = node;
		}

		// increment the number of system calls being traced
		current->st_md_base->count++;
	}

	else if (action == REMOVE_STRACE)
	{
		// check if the syscall being added is present in the list or not
		struct strace_info *curr = current->st_md_base->next;
		struct strace_info *prev = NULL;
		prev->next = curr;
		int isPresent = 0;
		while (curr != NULL)
		{
			if (curr->syscall_num == syscall_num)
			{
				isPresent = 1;
				break;
			}
			prev = curr;
			curr = curr->next;
		}

		// if system call was not present return error
		if (isPresent == 0)
		{
			return -EINVAL;
		}

		// remove the system call from the list
		if (prev == NULL)
		{
			current->st_md_base->next = curr->next;
		}
		else
		{
			prev->next = curr->next;
		}
		if (curr->next == NULL)
			current->st_md_base->last = prev;
		os_free(curr, sizeof(struct strace_info));
		current->st_md_base->count--;
	}
	return 0;
}

// syscall to read traced information from trace buffer
int sys_read_strace(struct file *filep, char *buff, u64 count)
{
	// if no file pointer provided
	if (filep == NULL)
	{
		return -EINVAL;
	}

	// store the number of bytes read
	u32 numBytesRead = 0;

	// define the front and rear of the trace buffer queue
	int front = filep->trace_buffer->write_offset;
	int rear = filep->trace_buffer->read_offset;

	while (count--)
	{
		while(filep->trace_buffer->buffer[rear] != ' ') {
			buff[numBytesRead] = filep->trace_buffer->buffer[rear];
			numBytesRead = numBytesRead + 1;

			rear = (rear + 1) % TRACE_BUFFER_MAX_SIZE;
		}

		// nothing more to read
		if (rear == front)
		{
			filep->trace_buffer->write_offset = -1;
			filep->trace_buffer->read_offset = -1;
			front = rear = -1;
			break;
		}

		// skip over the delimiter
		rear = (rear + 1) % TRACE_BUFFER_MAX_SIZE;

	}

	// update the trace buffer offset
	filep->trace_buffer->read_offset = rear;

	return numBytesRead;
}

// syscall to start tracing
int sys_start_strace(struct exec_context *current, int fd, int tracing_mode)
{
	if (current == NULL)
	{
		return -EINVAL;
	}

	if (fd < 0)
	{
		return -EINVAL;
	}

	if(tracing_mode != FULL_TRACING && tracing_mode != FILTERED_TRACING) {
		return -EINVAL;
	}

	// if the head of the list has not been allocated yet, do it now
	if (current->st_md_base == NULL)
	{
		// printk("list allocated in sys_start_stace()\n");
		struct strace_head *listHead = os_alloc(sizeof(struct strace_head));
		listHead->count = 0;
		listHead->next = listHead->last = NULL;
		current->st_md_base = listHead;
	}

	// initialize the other members of the head of the systrace list properly
	current->st_md_base->strace_fd = fd;
	current->st_md_base->is_traced = current->pid;
	current->st_md_base->tracing_mode = tracing_mode;

	return 0;
}

// syscall to end tracing
int sys_end_strace(struct exec_context *current)
{
	if (current == NULL)
	{
		return -EINVAL;
	}

	current->st_md_base->is_traced = 0;
	struct strace_head *listHead = current->st_md_base;
	struct strace_info *temp = listHead->next;

	while (temp != listHead->last->next)
	{
		struct strace_info *nodeToBeFreed = temp;
		os_free(nodeToBeFreed, sizeof(struct strace_info));
		temp = temp->next;
	}

	os_free(listHead, sizeof(struct strace_head));

	return 0;
}



///////////////////////////////////////////////////////////////////////////
//// 		Start of ftrace functionality 		      	      /////
///////////////////////////////////////////////////////////////////////////

// adds a function to the ftrace list
long add_ftrace(struct exec_context *ctx, unsigned long faddr, long nargs, int fd_trace_buffer)
{
	// if max number of functions to be traced is already reached
	if (ctx->ft_md_base->count == FTRACE_MAX)
	{
		return -EINVAL;
	}

	// if the function being added already exists in the list
	struct ftrace_info *temp = ctx->ft_md_base->next;
	while(temp != NULL)
	{
		if (temp->faddr == faddr)
		{
			return -EINVAL;
		}
		temp = temp->next;
	}

	// allocate the ftrace node and initialize its members
	struct ftrace_info *listNode = os_alloc(sizeof(struct ftrace_info));
	listNode->faddr = faddr;
	listNode->num_args = nargs;
	listNode->fd = fd_trace_buffer;
	listNode->capture_backtrace = 0;
	listNode->next = NULL;
	listNode->code_backup[0] = listNode->code_backup[1] = 0;
	listNode->code_backup[2] = listNode->code_backup[3] = 0;

	if (ctx->ft_md_base->last == NULL)
	{
		ctx->ft_md_base->last = ctx->ft_md_base->next = listNode;
	}
	else {
		ctx->ft_md_base->last->next = listNode;
		ctx->ft_md_base->last = listNode;
	}

	// increment the count of functions to be traced
	ctx->ft_md_base->count++;

	return 0;
}

// specifies ftrace operations
long do_ftrace(struct exec_context *ctx, unsigned long faddr, long action, long nargs, int fd_trace_buffer)
{
	// if no proper context is passed
	if (ctx == NULL)
	{
		return -EINVAL;
	}

	// input error
	if (nargs < 0 || fd_trace_buffer < 0)
	{
		return -EINVAL;
	}

	// if ftrace_head list hasn't been initialized yet
	if (ctx->ft_md_base == NULL)
	{
		struct ftrace_head *listHead = os_alloc(sizeof(struct ftrace_head));
		listHead->count = 0;
		listHead->next = listHead->last = NULL;
		ctx->ft_md_base = listHead;
	}

	if(action == ADD_FTRACE) return add_ftrace(ctx,faddr,nargs,fd_trace_buffer);

	// if the function hasn't been already added to tracing list
	struct ftrace_info *node = ctx->ft_md_base->next;
	struct ftrace_info *prev = NULL;
	prev->next = node;
	int flag = 0;
	while (node != NULL)
	{
		if (node->faddr == faddr)
		{
			flag = 1;
			break;
		}
		prev = node;
		node = node->next;
	}
	if (flag == 0)
	{
		return -EINVAL;
	}

	if (action == ENABLE_FTRACE)
	{
		// save the original address of the function before modification
		unsigned char *modifyAddr = (unsigned char *)(faddr);
		node->code_backup[0] = *modifyAddr;
		node->code_backup[1] = *(modifyAddr + 1);
		node->code_backup[2] = *(modifyAddr + 2);
		node->code_backup[3] = *(modifyAddr + 3);
		
		// modify address by INV_OPCODE
		*modifyAddr = INV_OPCODE;
		*(modifyAddr+1) = INV_OPCODE;
		*(modifyAddr+2) = INV_OPCODE;
		*(modifyAddr+3) = INV_OPCODE;

	}

	else if (action == REMOVE_FTRACE)
	{

		// check if tracing was enabled for this function
		if (node->code_backup[0] != 0)
		{
			// restore the original address for this function
			unsigned char *origAddr = (unsigned char *)faddr;
			*origAddr = node->code_backup[0];
			*(origAddr + 1) = node->code_backup[1];
			*(origAddr + 2) = node->code_backup[2];
			*(origAddr + 3) = node->code_backup[3];
		}

		// remove the function from list of traced functions
		if (prev == NULL)
		{
			ctx->ft_md_base->next = node->next;
		}
		else
		{
			prev->next = node->next;
		}
		if (node->next == NULL)
			ctx->ft_md_base->last = prev;

		// free the allocated space
		os_free(node, sizeof(struct ftrace_info));

		// decrement the number of functions being added to the list
		ctx->ft_md_base->count--;
	}

	else if (action == DISABLE_FTRACE)
	{
		if (node->code_backup[0] != 0)
		{
			unsigned char *origAddr = (unsigned char *)faddr;
			*origAddr = node->code_backup[0];
			*(origAddr + 1) = node->code_backup[1];
			*(origAddr + 2) = node->code_backup[2];
			*(origAddr + 3) = node->code_backup[3];
		}
	}

	else if(action == ENABLE_BACKTRACE) {
		
		// check if tracing has been enabled or not
		if(node->code_backup[0] == 0) {

			// save the original address of the function before modification
			unsigned char *modifyAddr = (unsigned char *)(faddr);
			node->code_backup[0] = *modifyAddr;
			node->code_backup[1] = *(modifyAddr + 1);
			node->code_backup[2] = *(modifyAddr + 2);
			node->code_backup[3] = *(modifyAddr + 3);

			// modify address by INV_OPCODE
			*modifyAddr = INV_OPCODE;
			*(modifyAddr+1) = INV_OPCODE;
			*(modifyAddr+2) = INV_OPCODE;
			*(modifyAddr+3) = INV_OPCODE;
		}
		node->capture_backtrace = 1;
	}

	else if(action == DISABLE_BACKTRACE) {
		
		node->capture_backtrace = 0;

		// check if tracing has been enabled or not
		if(node->code_backup[0] != 0) {

			unsigned char *origAddr = (unsigned char *)faddr;
			*origAddr = node->code_backup[0];
			*(origAddr + 1) = node->code_backup[1];
			*(origAddr + 2) = node->code_backup[2];
			*(origAddr + 3) = node->code_backup[3];
		}
	}

	return 0;
}

// Fault handler
long handle_ftrace_fault(struct user_regs *regs)
{
	struct exec_context* ctx = get_current_ctx();

	// get the address of the function which has called the fault
	long faddr = regs->entry_rip;

	// get the fd of the trace buffer which will be written to
	struct ftrace_info* node = ctx->ft_md_base->next;
	while(node != NULL) {
		if(node->faddr == faddr) break;
		node = node->next;
	}

	int fd = node->fd;
	struct file* filep = ctx->files[fd];
	u64 reg;
	u64 read_val;

	// write the address of the function to the trace buffer
	write_to_buffer(filep,(unsigned char*)&faddr,8);
	
	// write the function arguments to the trace buffer
	if(node->num_args > 0) {
		reg = regs->rdi;
		write_to_buffer(filep,(unsigned char*)&reg,8);
	}

	if(node->num_args > 1) {
		reg = regs->rsi;
		write_to_buffer(filep,(unsigned char*)&reg,8);
	}

	if(node->num_args > 2) {
		reg = regs->rdx;
		write_to_buffer(filep,(unsigned char*)&reg,8);
	}

	if(node->num_args > 3) {
		reg = regs->rcx;
		write_to_buffer(filep,(unsigned char*)&reg,8);
	}

	if(node->num_args > 4) {
		reg = regs->r8;
		write_to_buffer(filep,(unsigned char*)&reg,8);
	}

	// manipulate the registers so that function executes the first instruction correctly
	
	regs->entry_rsp = regs->entry_rsp - 8;
	u64* sptr = (u64 *)(regs->entry_rsp);
	*sptr = regs->rbp;
	regs->rbp = regs->entry_rsp;

	// increment the program counter
	regs->entry_rip = regs->entry_rip + 4;

	// if backtrace is on
	if(node->capture_backtrace == 1) {

		// capture the address of the first instruction of this function
		write_to_buffer(filep,(char*)&faddr,8);

		// create an array to store the return addresses of the function calls
		u64* func_addrs = (u64*)os_page_alloc(USER_REG);
		// store the number of function calls
		int func_count = 0;
		// save the value of the frame pointer
		u64 rbp_ptr = *((u64*)regs->rbp);
		// store the return address of function using the frame pointer
		u64 return_addr = *((u64*)(regs->rbp + 8));  

		// keep saving function return addresses until you reach the main() return address
		while(return_addr != END_ADDR) {
			func_addrs[func_count++] = return_addr;
			return_addr = *((u64*)(rbp_ptr + 8));
			rbp_ptr = *((u64*)(rbp_ptr));
		} 

		// write the backtrace info on the buffer
		for(int i = 0; i < func_count; i++) {
			write_to_buffer(filep,(unsigned char*)&func_addrs[i],8);
		}

		// deallocate the memory
		os_page_free(USER_REG,func_addrs);
	}

	// add a delimiter to the trace buffer
	char* delimiter = (char*)(' ');
	write_to_buffer(filep,(char*)&delimiter,1);

	return 0;
}

// reads ftrace info from the trace buffer
int sys_read_ftrace(struct file *filep, char *buff, u64 count)
{
	if (filep == NULL)
	{
		return -EINVAL;
	}

	u64 retval = 0;
	u32 numBytesRead = 0;

	int front = filep->trace_buffer->write_offset;
	int rear = filep->trace_buffer->read_offset;


	// in case the trace buffer is empty
	if (front == -1 && rear == -1)
	{
		return -EINVAL;
	}

	while(count--)
	{
		while(filep->trace_buffer->buffer[rear] != ' ') {
			buff[numBytesRead] = filep->trace_buffer->buffer[rear];
			numBytesRead = numBytesRead + 1;

			rear = (rear + 1) % TRACE_BUFFER_MAX_SIZE;
		}

		// nothing more to read
		if (rear == front)
		{
			filep->trace_buffer->write_offset = -1;
			filep->trace_buffer->read_offset = -1;
			front = rear = -1;
			break;
		}

		// skip over the delimiter
		rear = (rear + 1) % TRACE_BUFFER_MAX_SIZE;
	}

	// update the trace buffer offset
	filep->trace_buffer->read_offset = rear;

	return numBytesRead;
}