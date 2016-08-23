#--------------------------------------------
# Definition of compiler option
#--------------------------------------------

CFLAGS		+=	\
	-I./		\

LDFLAGS 	?=

LIBS		:=  	\
	-lmediactl		\
	-lv4l2subdev	\
	-lmmngr			\
	-lmmngrbuf		\

OPT=

#--------------------------------------------
# target and obj
#--------------------------------------------

TARGET	= v4l2_hgo_tp

OBJS	=			\
	v4l2_hgo_tp.o	\

#--------------------------------------------
# make rule
#--------------------------------------------

.c.o	:
	@echo compile $< ...
	@$(CC) $(CFLAGS) $(OPT) -Wall -c -o $@ $<

$(TARGET): $(OBJS)
	$(CC) -o $@ $+ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
	rm -f $(OBJS)

all:
	make clean
	make $(TARGET)

m3:
	make clean
	make $(TARGET) OPT=-DUSE_M3
