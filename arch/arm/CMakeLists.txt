PROJECT(arm)

ADD_LIBRARY(arm STATIC
			arm_disasm.cpp
			arm_arch.cpp
			arm_translate.cpp)
TARGET_LINK_LIBRARIES(arm ${CMAKE_DL_LIBS})
