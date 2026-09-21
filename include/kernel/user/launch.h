#ifndef KERNEL_USER_LAUNCH_H
#define KERNEL_USER_LAUNCH_H

/* BSP, IF=0, before scheduler startup. Loads the archive programs, grants
 * resources and submits them. Failures unwind unsubmitted processes. */
void user_launch_initial(void);

#endif
