#--------------------------------------------------------
# The CMakeLists.txt for:                      pGenRescue
# Author(s):                                Karolos Geroulanos
#--------------------------------------------------------

FILE(GLOB SRC *.cpp)

ADD_EXECUTABLE(pGenRescue ${SRC})

TARGET_LINK_LIBRARIES(pGenRescue
   ${MOOS_LIBRARIES}
   geometry
   mbutil
   m
   pthread)

