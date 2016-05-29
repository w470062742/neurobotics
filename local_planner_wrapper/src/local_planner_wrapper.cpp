#include <local_planner_wrapper/local_planner_wrapper.h>
#include <pluginlib/class_list_macros.h>

#include <math.h>

// Register this planner as a BaseLocalPlanner plugin
PLUGINLIB_EXPORT_CLASS(local_planner_wrapper::LocalPlannerWrapper, nav_core::BaseLocalPlanner)

namespace local_planner_wrapper
{
    // Constructor
    // --> Part of interface
    LocalPlannerWrapper::LocalPlannerWrapper() : initialized_(false),
                                                 blp_loader_("nav_core", "nav_core::BaseLocalPlanner")
    {
    }

    // Desctructor
    // --> Part of interface
    LocalPlannerWrapper::~LocalPlannerWrapper()
    {
        tc_.reset();
    }

    // Initialize the planner
    // --> Part of interface
    // name:                some string, not important
    // tf:                  this will tell the planner the robots location (i think)
    // costmap_ros:         the costmap
    // Return:              nothing
    void LocalPlannerWrapper::initialize(std::string name, tf::TransformListener* tf,
                                         costmap_2d::Costmap2DROS* costmap_ros)
    {
        // If we are not ininialized do so
        if (!initialized_)
        {
            // Publishers subscribers
            ros::NodeHandle private_nh("~/" + name);
            g_plan_pub_ = private_nh.advertise<nav_msgs::Path>("global_plan", 1);
            l_plan_pub_ = private_nh.advertise<nav_msgs::Path>("local_plan", 1);
            //updated_costmap_pub_ = private_nh.advertise<nav_msgs::OccupancyGrid>("updated_costmap", 1);
            //costmap_sub_ = private_nh.subscribe("/move_base/local_costmap/costmap", 1000,
                                                //&LocalPlannerWrapper::filterCostmap, this);
            //costmap_update_sub_ = private_nh.subscribe("/move_base/local_costmap/costmap_updates", 1000,
                                                //&LocalPlannerWrapper::updateCostmap, this);

            laser_scan_sub_ = private_nh.subscribe("/scan", 1000, &LocalPlannerWrapper::getLaserScanPoints, this);


            state_pub_ = private_nh.advertise<std_msgs::Bool>("new_round", 1);

            customized_costmap_pub_ = private_nh.advertise<nav_msgs::OccupancyGrid>("customized_costmap", 1);

            marker_array_pub_ = private_nh.advertise<visualization_msgs::MarkerArray>("visualization_marker_array", 1); // to_delete

            // --- Just for testing: ---
            // initialization of cost map as only updates are received
            /*filtereded_costmap_ = nav_msgs::OccupancyGrid();

            filtereded_costmap_.header.frame_id = "/base_footprint";
            filtereded_costmap_.header.stamp = ros::Time::now();

            filtereded_costmap_.info.height = 80;
            filtereded_costmap_.info.width = 80;
            filtereded_costmap_.info.resolution = 0.05;
            filtereded_costmap_.info.origin.position.x = -1.95;
            filtereded_costmap_.info.origin.position.y = -1.95;
            filtereded_costmap_.info.origin.position.z = 0.0;
            filtereded_costmap_.info.origin.orientation.x = 0.0;
            filtereded_costmap_.info.origin.orientation.y = 0.0;
            filtereded_costmap_.info.origin.orientation.z = 0.0;
            filtereded_costmap_.info.origin.orientation.w = 1.0;

            std::vector<int8_t> data(6400,0);
            filtereded_costmap_.data = data;*/


            // -------------------------------------

            is_customized_costmap_initialized_ = false;

            // Setup tf
            tf_ = tf;

            // Setup the costmap_ros interface
            costmap_ros_ = costmap_ros;
            costmap_ros_->getRobotPose(current_pose_);

            // Get the actual costmap object
            costmap_ = costmap_ros_->getCostmap();

            // Should we use the dwa planner?
            existing_plugin_ = true;
            std::string local_planner = "base_local_planner/TrajectoryPlannerROS";

            // If we want to, lets load a local planner plugin to do the work for us
            if (existing_plugin_)
            {
                try
                {
                    tc_ = blp_loader_.createInstance(local_planner);
                    ROS_INFO("Created local_planner %s", local_planner.c_str());
                    tc_->initialize(blp_loader_.getName(local_planner), tf, costmap_ros);
                }
                catch (const pluginlib::PluginlibException& ex)
                {
                    ROS_FATAL("Failed to create plugin");
                    exit(1);
                }
            }

            // We are now initialized
            initialized_ = true;
        }
        else
        {
            ROS_WARN("This planner has already been initialized, doing nothing.");
        }
    }

    // Sets the plan
    // --> Part of interface
    // orig_global_plan:    this is the global plan we're supposed to follow (a vector of positions forms the
    //                      line)
    // Return:              True if plan was succesfully received...
    bool LocalPlannerWrapper::setPlan(const std::vector<geometry_msgs::PoseStamped>& orig_global_plan)
    {

        //std::cout << "DRIN" << std::endl;

        // Check if the planner has been initialized
        if (!initialized_)
        {
            ROS_ERROR("This planner has not been initialized, please call initialize() before using this planner");
            return false;
        }

        // Safe the global plan
        global_plan_.clear();
        global_plan_ = orig_global_plan;

        // Set the goal position so we can check if we have arrived or not
        goal_.position.x = orig_global_plan.at(orig_global_plan.size() - 1).pose.position.x;
        goal_.position.y = orig_global_plan.at(orig_global_plan.size() - 1).pose.position.y;

        // If we use the dwa:
        // This code is copied from the dwa_planner
        if (existing_plugin_)
        {
            if(tc_->setPlan(orig_global_plan))
            {
                ROS_ERROR("Successfully set plan!!!");
            }
        }
        return true;
    }

    // Compute the velocity commands
    // --> Part of interface
    // cmd_vel:             fill this vector with our velocity commands (the actual output we're producing)
    // Return:              True if we didn't fail
    bool LocalPlannerWrapper::computeVelocityCommands(geometry_msgs::Twist& cmd_vel)
    {
        // Should we use the network as a planner or the dwa planner?
        if (!existing_plugin_)
        {
            // Lets drive in circles
            cmd_vel.angular.z = 0.1;
            cmd_vel.linear.x = 0.1;
            return true;
        }
        // Use the existing local planner plugin
        else
        {
            geometry_msgs::Twist cmd;

            if(tc_->computeVelocityCommands(cmd))
            {
                cmd_vel = cmd;
                return true;
            }
            else
            {
                ROS_ERROR("Failed computing a command");
                return false;
            }
        }
    }


    // Tell if goal was reached
    // --> Part of interface
    // Return:              True if goal pose was reached
    bool LocalPlannerWrapper::isGoalReached()
    {
        // Get current position
        costmap_ros_->getRobotPose(current_pose_);

        // Get distance from position to goal, probably there is a better way to do this
        double dist = sqrt(pow((current_pose_.getOrigin().getX() - goal_.position.x
                                + costmap_->getSizeInMetersX()/2), 2.0)
                           + pow((current_pose_.getOrigin().getY()  - goal_.position.y
                                  + costmap_->getSizeInMetersY()/2), 2.0));

        // More or less an arbitrary number. With above dist calculation this seems to be te best the robot can do...
        if(dist < 0.2)
        {
            ROS_INFO("We made it to the goal!");

            // Publish that a new round can be started with the stage_sim_bot
            std_msgs::Bool new_round;
            new_round.data = true;
            state_pub_.publish(new_round);
            global_plan_.clear();
            return true;
        }
        else
        {
            return false;
        }
    }


    // Callback function for the subscriber to the local costmap update
    // costmap_update:      this is the costmap message
    // Return:              nothing
    /*void LocalPlannerWrapper::updateCostmap(map_msgs::OccupancyGridUpdate costmap_update) {

        //std::cout << "Costmap update received -> update costmap!!!" << std::endl;

        int index = 0;

        for(int y = costmap_update.y; y < costmap_update.y + costmap_update.height; y++)
        {
            for(int x = costmap_update.x; x < costmap_update.x + costmap_update.width; x++)
            {
                filtereded_costmap_.data[getIndex(x,y)] = costmap_update.data[index++];
            }
        }

        filtereded_costmap_.header = costmap_update.header;

        filterCostmap(filtereded_costmap_);

    }*/


    // Get index for costmap update
    // x:
    // y:
    // Return:
    /*int LocalPlannerWrapper::getIndex(int x, int y)
    {
        int costmap_width = filtereded_costmap_.info.width;
        return y * costmap_width + x;
    }*/


    // Callback function for the subscriber to the local costmap
    // costmap:             this is the costmap message
    // Return:              nothing
    /*void LocalPlannerWrapper::filterCostmap(nav_msgs::OccupancyGrid costmap)
    {
        filtereded_costmap_ = costmap;

        // Get costmap size
        int width = filtereded_costmap_.info.width;
        int height = filtereded_costmap_.info.height;

        // Change the costmap
        for (int i = 0; i < height; i++)
        {
            for (int j = 0; j < width; j++)
            {
                if (filtereded_costmap_.data[i * width + j] < 100)
                {
                    filtereded_costmap_.data[i * width + j] = 50;
                }
                else
                {
                    filtereded_costmap_.data[i * width + j] = 100;
                }
            }
        }

        // Transform the global plan into costmap coordinates
        unsigned int x, y;
        geometry_msgs::PoseStamped pose_fixed_frame; // pose given in fixed frame of global plan which is by default "map"
        geometry_msgs::PoseStamped pose_robot_base_frame; // pose given in global frame of the local cost map

        for(std::vector<geometry_msgs::PoseStamped>::iterator it = global_plan_.begin(); it != global_plan_.end(); it++)
        {
            // Transform pose from fixed frame of global plan to global frame of local cost map
            pose_fixed_frame = *it;
            try
            {
                pose_fixed_frame.header.stamp = costmap.header.stamp;
                tf_->transformPose(costmap.header.frame_id, pose_fixed_frame, pose_robot_base_frame);
            }
            catch (tf::TransformException ex)
            {
                ROS_ERROR("%s",ex.what());
            }

            // Transformtion to costmap coordinates
            if (costmap_->worldToMap(pose_robot_base_frame.pose.position.x, pose_robot_base_frame.pose.position.y, x, y))
            {
                filtereded_costmap_.data[x + y*width] = 0;
            }
        }

        updated_costmap_pub_.publish(filtereded_costmap_);
    }*/

    void LocalPlannerWrapper::initializeCustomizedCostmap()
    {
        customized_costmap_ = nav_msgs::OccupancyGrid();

        // header
        customized_costmap_.header.frame_id = "/base_footprint";
        customized_costmap_.header.stamp = ros::Time::now();
        customized_costmap_.header.seq = 0;

        // info
        customized_costmap_.info.width = costmap_->getSizeInCellsX(); // e.g. 80
        customized_costmap_.info.height = costmap_->getSizeInCellsY(); // e.g. 80
        customized_costmap_.info.resolution = costmap_->getResolution(); // e.g. 0.05
        customized_costmap_.info.origin.position.x = -costmap_->getSizeInMetersX()/2.0; // e.g.-1.95
        customized_costmap_.info.origin.position.y = -costmap_->getSizeInMetersY()/2.0; // e.g.-1.95
        customized_costmap_.info.origin.position.z = 0.0;
        customized_costmap_.info.origin.orientation.x = 0.0;
        customized_costmap_.info.origin.orientation.y = 0.0;
        customized_costmap_.info.origin.orientation.z = 0.0;
        customized_costmap_.info.origin.orientation.w = 1.0;
        // customized_costmap_.info.map_load_time important?

        // data
        std::vector<int8_t> data(customized_costmap_.info.width*customized_costmap_.info.height,50);
        customized_costmap_.data = data;
    }

    // Callback function for the subscriber to the laser scan
    // laser_scan:          this is the laser scan message
    // Return:              nothing
    void LocalPlannerWrapper::getLaserScanPoints(sensor_msgs::LaserScan laser_scan)
    {
        if (!is_customized_costmap_initialized_) // initialize costmap
        {
            initializeCustomizedCostmap();
            is_customized_costmap_initialized_ = true;
        } else { // clear costmap -> set all pixel of costmap to same value e.g. 50
            std::vector<int8_t> data(customized_costmap_.info.width*customized_costmap_.info.height,50);
            customized_costmap_.data = data;
        }

        // get source frame and target frame of laser scan points
        std::string laser_scan_source_frame = laser_scan.header.frame_id;
        std::string laser_scan_target_frame = customized_costmap_.header.frame_id;

        ros::Time laser_scan_stamp = laser_scan.header.stamp; // stamp of first laser point in range
        // std::cout << "Laser scan stamp: " << laser_scan_stamp << std::endl;
        // std::cout << "Time increment: " << laser_scan.time_increment << std::endl;
        // std::cout << "Scan time: " << laser_scan.scan_time << std::endl;
        ros::Time customized_costmap_stamp = laser_scan_stamp;

        tf::StampedTransform transform; // transformation between robot base frame and frame of laser scan
        try
        {
            //tf_->lookupTransform(laser_scan_target_frame, customized_costmap_stamp, laser_scan_source_frame, laser_scan_stamp, "/map", transform);
            tf_->lookupTransform(laser_scan_target_frame, laser_scan_source_frame, laser_scan_stamp, transform);
        }
        catch (tf::TransformException ex)
        {
            ROS_ERROR("%s",ex.what());
        }

        double x_position_laser_scan_frame; // x position of laser scan point in frame of laser scan
        double y_position_laser_scan_frame; // y position of laser scan point in frame of laser scan
        double x_position_robot_base_frame; // x position of laser scan point in robot base frame
        double y_position_robot_base_frame; // y position of laser scan point in robot base frame

        marker_array_.markers.clear();

        // std::cout << "Laser scan RANGE MIN: " << laser_scan.range_min << std::endl;
        // std::cout << "Laser scan RANGE Max: " << laser_scan.range_max << std::endl;

        for(int i = 0; i < laser_scan.ranges.size(); i++)
        {
            if ((laser_scan.ranges.at(i) > laser_scan.range_min) && (laser_scan.ranges.at(i) < laser_scan.range_max))
            {

                //laser_scan_source_stamp = laser_scan_source_stamp + ros::Duration(laser_scan.scan_time); // as robot base is moving laser_scan_source_stamp is different for every laser scan point

                // std::cout << "range at position " << i << " is: " << laser_scan.ranges.at(i)  << std::endl;

                // get x and y coordinates of laser scan point in frame of laser scan, z coordinate is ignored as we are working with a 2D costmap
                x_position_laser_scan_frame = laser_scan.ranges.at(i) * cos(laser_scan.angle_min + i*laser_scan.angle_increment);
                y_position_laser_scan_frame = laser_scan.ranges.at(i) * sin(laser_scan.angle_min + i*laser_scan.angle_increment);

                // translation
                x_position_robot_base_frame = x_position_laser_scan_frame + transform.getOrigin().getX();
                y_position_robot_base_frame = y_position_laser_scan_frame + transform.getOrigin().getY();
                // rotation
                double roll, pitch, yaw;
                transform.getBasis().getRPY(roll, pitch, yaw);
                double x_temp = x_position_robot_base_frame;
                double y_temp = y_position_robot_base_frame;
                x_position_robot_base_frame = cos(yaw)*x_temp - sin(yaw)*y_temp;
                y_position_robot_base_frame = sin(yaw)*x_temp + cos(yaw)*y_temp;

                // visualization
                visualization_msgs::Marker marker;
                marker.header.frame_id = laser_scan_target_frame;
                marker.header.stamp = customized_costmap_stamp;

                marker.id = i;

                marker.type = visualization_msgs::Marker::SPHERE;
                marker.action = visualization_msgs::Marker::ADD;

                marker.pose.position.x = x_position_robot_base_frame;
                marker.pose.position.y = y_position_robot_base_frame;
                marker.pose.position.z = 0;
                marker.pose.orientation.x = 0.0;
                marker.pose.orientation.y = 0.0;
                marker.pose.orientation.z = 0.0;
                marker.pose.orientation.w = 1.0;

                marker.scale.x = 0.05;
                marker.scale.y = 0.05;
                marker.scale.z = 0.05;

                marker.color.r = 0.0f;
                marker.color.g = 1.0f;
                marker.color.b = 0.0f;
                marker.color.a = 1.0;

                marker_array_.markers.push_back(marker);

                // Transformtion to costmap coordinates
                int x, y;

                x = round(((x_position_robot_base_frame - customized_costmap_.info.origin.position.x)/costmap_->getSizeInMetersX())*customized_costmap_.info.width);
                y = round(((y_position_robot_base_frame - customized_costmap_.info.origin.position.y)/costmap_->getSizeInMetersY())*customized_costmap_.info.height);

                if ((x >=0) && (y >=0) && (x < customized_costmap_.info.width) && (y < customized_costmap_.info.height))
                {
                    customized_costmap_.data[x + y*customized_costmap_.info.width] = 0;
                }
            }
        }

        //std::cout << "Marker array size:" << marker_array_.markers.size() << std::endl;
        marker_array_pub_.publish(marker_array_);

        customized_costmap_.header.stamp = customized_costmap_stamp;
        customized_costmap_pub_.publish(customized_costmap_);

        customized_costmap_.header.seq = customized_costmap_.header.seq + 1;

        //customized_costmap_.header.stamp = laser_scan_source_stamp + ros::Time::fromSec(laser_scan.ranges.size()*laser_scan.scan_time);


    }

};
