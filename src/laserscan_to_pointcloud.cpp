/**\file LaserScanToPointcloud.cpp
 * \brief Implementation of a PointCloud2 builder from LaserScans.
 *
 * @version 1.0
 * @author Carlos Miguel Correia da Costa
 */

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <includes>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
#include "laserscan_to_pointcloud/laserscan_to_pointcloud.h"
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </includes>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <imports>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </imports>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<


namespace laserscan_to_pointcloud {
// =============================================================================  <public-section>   ===========================================================================
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <constructors-destructor>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
LaserScanToPointcloud::LaserScanToPointcloud(std::string target_frame, double min_range_cutoff_percentage, double max_range_cutoff_percentage) :
		target_frame_(target_frame),
		min_range_cutoff_percentage_offset_(min_range_cutoff_percentage), max_range_cutoff_percentage_offset_(max_range_cutoff_percentage),
		include_laser_intensity_(false),
		number_of_pointclouds_created_(0),
		number_of_points_in_cloud_(0),
		number_of_scans_assembled_in_current_pointcloud_(0),
		polar_to_cartesian_matrix_angle_min_(0), polar_to_cartesian_matrix_angle_max_(0), polar_to_cartesian_matrix_angle_increment_(0) {

	polar_to_cartesian_matrix_.resize(Eigen::NoChange, 0);
}

LaserScanToPointcloud::~LaserScanToPointcloud() {}
// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </constructors-destructor>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<



// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   <LaserScanToPointcloud-functions>   <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void LaserScanToPointcloud::initNewPointCloud(size_t number_of_reserved_points, bool include_laser_intensity) {
	include_laser_intensity_ = include_laser_intensity;
	pointcloud_ = sensor_msgs::PointCloud2Ptr(new sensor_msgs::PointCloud2());
	number_of_points_in_cloud_ = 0;
	number_of_scans_assembled_in_current_pointcloud_ = 0;

	pointcloud_->header.seq = number_of_pointclouds_created_++;
	pointcloud_->header.stamp = ros::Time::now();
	pointcloud_->header.frame_id = target_frame_;
	pointcloud_->height = 1;
	pointcloud_->width = 0;
	pointcloud_->fields.clear();
	pointcloud_->fields.resize(include_laser_intensity_ ? 4 : 3);
	pointcloud_->fields[0].name = "x";
	pointcloud_->fields[0].offset = 0;
	pointcloud_->fields[0].datatype = sensor_msgs::PointField::FLOAT32;
	pointcloud_->fields[0].count = 1;
	pointcloud_->fields[1].name = "y";
	pointcloud_->fields[1].offset = 4;
	pointcloud_->fields[1].datatype = sensor_msgs::PointField::FLOAT32;
	pointcloud_->fields[1].count = 1;
	pointcloud_->fields[2].name = "z";
	pointcloud_->fields[2].offset = 8;
	pointcloud_->fields[2].datatype = sensor_msgs::PointField::FLOAT32;
	pointcloud_->fields[2].count = 1;
	pointcloud_->point_step = 12;

	if (include_laser_intensity_) {
		pointcloud_->fields[3].name = "intensity";
		pointcloud_->fields[3].offset = 12;
		pointcloud_->fields[3].datatype = sensor_msgs::PointField::FLOAT32;
		pointcloud_->fields[3].count = 1;
		pointcloud_->point_step += 4;
	}

	pointcloud_->row_step = 0;
	pointcloud_->data.reserve(number_of_reserved_points * pointcloud_->point_step);
	pointcloud_->is_dense = true;
}


bool LaserScanToPointcloud::updatePolarToCartesianProjectionMatrix(const sensor_msgs::LaserScanConstPtr& laser_scan) {
	size_t number_of_scan_points = laser_scan->ranges.size();
	if (polar_to_cartesian_matrix_.cols() != number_of_scan_points
			/*|| polar_to_cartesian_matrix_angle_min_ != laser_scan->angle_min
			|| polar_to_cartesian_matrix_angle_max_ != laser_scan->angle_max
			|| polar_to_cartesian_matrix_angle_increment_ != laser_scan->angle_increment*/) { // todo: fix float compare

		ROS_DEBUG_STREAM("Updating polar to cartesian projection matrix with ->" \
				<< "\n\t[ranges.size()]:" << laser_scan->ranges.size() \
				<< "\n\t[angle_min]:" << laser_scan->angle_min \
				<< "\n\t[angle_max]:" << laser_scan->angle_max
				<< "\n\t[increment]:" << laser_scan->angle_increment);

		// recompute sin and cos values
		polar_to_cartesian_matrix_.resize(Eigen::NoChange, laser_scan->ranges.size());
		polar_to_cartesian_matrix_angle_min_ = laser_scan->range_min;
		polar_to_cartesian_matrix_angle_max_ = laser_scan->range_max;
		polar_to_cartesian_matrix_angle_increment_ = laser_scan->angle_increment;

		double current_angle = laser_scan->angle_min;
		for (size_t point_pos = 0; point_pos < number_of_scan_points; ++point_pos) {
			polar_to_cartesian_matrix_(0, point_pos) = std::cos(current_angle);
			polar_to_cartesian_matrix_(1, point_pos) = std::sin(current_angle);
			current_angle += laser_scan->angle_increment;
		}

		return true;
	}

	return false;
}


bool LaserScanToPointcloud::integrateLaserScanWithShpericalLinearInterpolation(const sensor_msgs::LaserScanConstPtr& laser_scan) {
	// laser info
	size_t number_of_scan_points = laser_scan->ranges.size();
	size_t number_of_scan_steps = number_of_scan_points - 1;
	ros::Duration scan_duration(number_of_scan_steps * laser_scan->time_increment);
	ros::Time scan_start_time = laser_scan->header.stamp;
	ros::Time scan_end_time = scan_start_time + scan_duration;

	// tfs setup
	std::vector<tf2::Transform> collected_tfs;
	tf_collector_.collectTFs(pointcloud_->header.frame_id, laser_scan->header.frame_id, scan_start_time, scan_end_time, 2, collected_tfs);
	if (collected_tfs.empty()) { return false; }
	updatePolarToCartesianProjectionMatrix(laser_scan);

	// projection and transformation setup
	double min_range_cutoff = laser_scan->range_min * min_range_cutoff_percentage_offset_;
	double max_range_cutoff = laser_scan->range_max * max_range_cutoff_percentage_offset_;
	tf2Scalar one_scan_step_percentage = 1.0 / (double)number_of_scan_steps;
	tf2Scalar current_scan_percentage = 0;
	tf2::Transform point_transform;
	if (collected_tfs.size() == 1) { point_transform = collected_tfs[0]; }


	// laser scan projection and transformation
	pointcloud_->data.resize((number_of_points_in_cloud_ + laser_scan->ranges.size()) * pointcloud_->point_step); // resize to fit all points in the LaserScan
	float* pointcloud_data_position = (float*)(&pointcloud_->data[number_of_points_in_cloud_ * pointcloud_->point_step]);
	for (size_t point_pos = 0; point_pos < number_of_scan_points; ++point_pos) {
		float point_range_value = laser_scan->ranges[point_pos];
		if (point_range_value > min_range_cutoff && point_range_value < max_range_cutoff) {
			// project laser scan point in 2D (in the laser frame of reference)
			tf2::Vector3 projected_point(point_range_value * polar_to_cartesian_matrix_(0, point_pos), point_range_value * polar_to_cartesian_matrix_(1, point_pos), 0);

			// interpolate position and rotation
			if (collected_tfs.size() == 2) {
				point_transform.getOrigin().setInterpolate3(collected_tfs[0].getOrigin(), collected_tfs[1].getOrigin(), current_scan_percentage);
				point_transform.setRotation(tf2::slerp(collected_tfs[0].getRotation(), collected_tfs[1].getRotation(), current_scan_percentage));
			}

			// transform point to target frame of reference
			tf2::Vector3 transformed_point = point_transform * projected_point;

			// copy point to pointcloud
			*pointcloud_data_position++ = (float)transformed_point.getX();
			*pointcloud_data_position++ = (float)transformed_point.getY();
			*pointcloud_data_position++ = (float)transformed_point.getZ();
			if (include_laser_intensity_) {
				if (point_pos < laser_scan->intensities.size()) {
					*pointcloud_data_position++ = (float)laser_scan->intensities[point_pos];
				} else {
					*pointcloud_data_position++ = 0;
				}
			}

			++number_of_points_in_cloud_;
		}
		current_scan_percentage += one_scan_step_percentage;
	}
	pointcloud_->width = number_of_points_in_cloud_;
	pointcloud_->row_step = pointcloud_->width * pointcloud_->point_step;
	pointcloud_->data.resize(pointcloud_->height * pointcloud_->row_step); // resize to shrink the vector size to the real number of points inserted

	++number_of_scans_assembled_in_current_pointcloud_;
	return true;
}

// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>   </LaserScanToPointcloud-functions>  <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
// =============================================================================  </public-section>   ==========================================================================

// =============================================================================   <protected-section>   =======================================================================
// =============================================================================   </protected-section>  =======================================================================

// =============================================================================   <private-section>   =========================================================================
// =============================================================================   </private-section>  =========================================================================
} /* namespace laserscan_to_pointcloud */
