# **************************************************************************
# * (C)Copyright 2020, Enjellic Systems Development, LLC. All rights reserved.
# **************************************************************************/


#
# Include micro-controller build definitions.
#
ifndef MCUCFGDIR
MCUCFGDIR=${shell cd ../../../..; pwd}
endif

include ${MCUCFGDIR}/STmcu.mk


#
# Variable definitions.
#
CINCLUDE = -I ${STMDIR}/Middlewares/Third_Party/FreeRTOS/Source/include \
	-I ${STMDIR}/Projects/Misc/RTOS/FreeRTOS/Inc			\
	-I ${STMDIR}/Projects/Misc/Samples/Cellular/Inc			\
	-I ${STMDIR}/Projects/32L496GDISCOVERY/Demonstrations/Cellular/STM32_Cellular/Config \
	-I ${STMDIR}/Projects/32L496GDISCOVERY/Demonstrations/Cellular/Core/Inc \
	-I ${STMDIR}/Drivers/CMSIS/Include				    \
	-I ${STMDIR}/Drivers/CMSIS/Device/ST/STM32L4xx/Include		    \
	-I ${STMDIR}/Drivers/STM32L4xx_HAL_Driver/Inc			    \
	-I ${STMDIR}/Drivers/BSP/X_STMOD_PLUS_MODEMS/BG96/AT_modem_bg96/Inc \
	-I ${STMDIR}/Middlewares/ST/STM32_Cellular/Core/Error/Inc	    \
	-I ${STMDIR}/Middlewares/ST/STM32_Cellular/Core/Trace/Inc	    \
	-I ${STMDIR}/Middlewares/ST/STM32_Cellular/Core/Runtime_Library/Inc \
	-I ../..							    \
	-I ${RTOSDIR}/CMSIS_RTOS

CFLAGS = ${CDEBUG} ${PLATFORM} ${DEFINES} -fno-diagnostics-color -Wall \
	${CINCLUDE}

CSRC = error_handler.c

LIBNAME = liberror_handler.a


#
# Compilation directives.
#
%.o: ${STMDIR}/Middlewares/ST/STM32_Cellular/Core/Error/Src/%.c
	${CC} ${CFLAGS} -c $< -o $@;


#
# Automatic definition of classes and objects.
#
COBJS = ${CSRC:.c=.o}


#
# Target directives.
#
# .PHONY: client lib ${SUBDIRS}


# Targets
${LIBNAME}: ${COBJS}
	${AR} r ${LIBNAME} $^;
	${RANLIB} ${LIBNAME};

clean:
	rm -f *.o ${LIBNAME};

distclean: clean
