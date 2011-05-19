 
################

# Subdirectories

################

 

DIRS := spu

 

################

# Target

################

 

PROGRAM_ppu	:= tema4

 

################

# Local Defines

################

IMPORTS		:= -llapack -lblas -lm spu/lib_tema4_spu.a -lspe2 -lpthread
CFLAGS 		:= -DDEBUG_ -DLOG_LEVEL=LOG_INFO

# imports the embedded simple_spu library

# allows consolidation of spu program into ppe binary

 

 

################

# make.footer

################

 

# make.footer is in the top of the SDK

ifdef CELL_TOP

include $(CELL_TOP)/buildutils/make.footer

else

include ../../../../buildutils/make.footer

endif

run:
	$(PROGRAM_ppu) images/tigru images/elefant 8 -c images/test 8
