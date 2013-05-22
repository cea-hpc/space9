#ifndef _SETTINGS_H
#define _SETTINGS_H

// shared with ganesha:
#define _9P_FID_PER_CONN        1024


// default values for conf
#define DEFAULT_UID      0
#define DEFAULT_RECV_NUM 100
#define DEFAULT_MSIZE    64*1024
#define DEFAULT_PORT     5640
#define DEFAULT_MAX_FID  1024

// max tag = recv_num for ganesha
#define DEFAULT_MAX_TAG  100


#endif
