#include "ros_head_pose_estimator.hpp"

#include "tf/transform_listener.h"

using namespace std;
using namespace cv;

// how many second in the *future* the face transformation should be published?
// this allow to compensate for the 'slowness' of face detection, but introduce
// some lag in TF.
#define TRANSFORM_FUTURE_DATING 0

HeadPoseEstimator::HeadPoseEstimator(ros::NodeHandle& rosNode,
                                     const string& prefix,
                                     const string& modelFilename):
            rosNode(rosNode),
            it(rosNode),
            facePrefix(prefix),
            estimator(modelFilename)

{
    sub = it.subscribeCamera("rgb", 1, &HeadPoseEstimator::detectFaces, this);

    nb_detected_faces_pub = rosNode.advertise<std_msgs::Char>("gazr/detected_faces/count", 1);
    face_poses_pub = rosNode.advertise<geometry_msgs::PoseArray>("gazr/detected_faces/poses", 1);

#ifdef HEAD_POSE_ESTIMATION_DEBUG
    pub = it.advertise("gazr/detected_faces/image",1);
#endif
}

void HeadPoseEstimator::detectFaces(const sensor_msgs::ImageConstPtr& rgb_msg, 
                                    const sensor_msgs::CameraInfoConstPtr& camerainfo)
{
    ROS_INFO_ONCE("First RGB image received .");

    // updating the camera model is cheap if not modified
    cameramodel.fromCameraInfo(camerainfo);

    estimator.focalLength = cameramodel.fx(); 
    estimator.opticalCenterX = cameramodel.cx();
    estimator.opticalCenterY = cameramodel.cy();

    // hopefully no copy here:
    //  - assignement operator of cv::Mat does not copy the data
    //  - toCvShare does no copy if the default (source) encoding is used.
    auto rgb = cv_bridge::toCvShare(rgb_msg, "bgr8")->image; 

    // got an empty image!
    if (rgb.size().area() == 0) return;

	//ROS_INFO_ONCE("test---test");

    /********************************************************************
    *                      Faces detection                           *
    ********************************************************************/

    auto all_features = estimator.update(rgb);

    auto poses = estimator.poses();

	ROS_INFO_ONCE("First RGB image received ..");
								
    std_msgs::Char nb_faces;
    nb_faces.data = poses.size();

    nb_detected_faces_pub.publish(nb_faces);

	cout << "nb_faces " << nb_faces << endl;

    geometry_msgs::PoseArray ros_poses;
    ros_poses.header = camerainfo->header;
    ros_poses.poses.resize(poses.size());

    for(size_t face_idx = 0; face_idx < poses.size(); ++face_idx) {

        auto trans = poses[face_idx];


        tf::Quaternion qrot;
        tf::Matrix3x3 mrot(
                trans(0,0), trans(0,1), trans(0,2),
                trans(1,0), trans(1,1), trans(1,2),
                trans(2,0), trans(2,1), trans(2,2));
        mrot.getRotation(qrot);

#if(false) // TAMS modification: do not pollute TF. Maybe optional at some point -- v4hn@20190820
        tf::Transform face_pose;
        face_pose.setOrigin( tf::Vector3( trans(0,3),
                                          trans(1,3),
                                          trans(2,3)) );
        face_pose.setRotation(qrot);
        tf::StampedTransform transform(face_pose, 
                rgb_msg->header.stamp,  // publish the transform with the same timestamp as the frame originally used
                cameramodel.tfFrame(),
                facePrefix + "_" + to_string(face_idx));
        br.sendTransform(transform);
#endif

        ros_poses.poses[face_idx].position.x= trans(0,3);
        ros_poses.poses[face_idx].position.y= trans(1,3);
        ros_poses.poses[face_idx].position.z= trans(2,3);

        ros_poses.poses[face_idx].orientation.x= qrot.x();
        ros_poses.poses[face_idx].orientation.y= qrot.y();
        ros_poses.poses[face_idx].orientation.z= qrot.z();
        ros_poses.poses[face_idx].orientation.w= qrot.w();

    }

    face_poses_pub.publish(ros_poses);

#ifdef HEAD_POSE_ESTIMATION_DEBUG
    if(pub.getNumSubscribers() > 0) {
        ROS_INFO_ONCE("Starting to publish face tracking output for debug");
        auto debugmsg = cv_bridge::CvImage(rgb_msg->header, "bgr8", estimator.drawDetections(rgb, all_features, poses)).toImageMsg();
        pub.publish(debugmsg);
    }
#endif
}

