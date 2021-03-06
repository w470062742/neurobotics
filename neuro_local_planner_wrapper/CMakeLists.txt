cmake_minimum_required(VERSION 2.8.3)
project(neuro_local_planner_wrapper)

find_package(catkin REQUIRED COMPONENTS
  base_local_planner
  costmap_2d
  nav_core
  nav_msgs
  pluginlib
  roscpp
  std_msgs
  tf
  message_generation
)

include_directories(
    include
    ${catkin_INCLUDE_DIRS}
    )

## Generate messages in the 'msg' folder
add_message_files(
  FILES
  Transition.msg
)

## Generate added messages and services with any dependencies listed here
generate_messages(
  DEPENDENCIES
  std_msgs
)


###################################
## catkin specific configuration ##
###################################
## The catkin_package macro generates cmake config files for your package
## Declare things to be passed to dependent projects
## INCLUDE_DIRS: uncomment this if you package contains header files
## LIBRARIES: libraries you create in this project that dependent projects also need
## CATKIN_DEPENDS: catkin_packages dependent projects also need
## DEPENDS: system dependencies of this project that dependent projects also need
catkin_package(
  INCLUDE_DIRS include
  LIBRARIES neuro_local_planner_wrapper
  CATKIN_DEPENDS base_local_planner costmap_2d nav_core nav_msgs pluginlib roscpp std_msgs tf message_runtime
  DEPENDS system_lib
)

add_library(neuro_local_planner_wrapper src/neuro_local_planner_wrapper.cpp)
target_link_libraries(neuro_local_planner_wrapper ${catkin_LIBRARIES})
