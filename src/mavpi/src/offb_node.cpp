
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <mavros_msgs/CommandBool.h>
#include <mavros_msgs/PositionTarget.h>
#include <mavros_msgs/SetMode.h>
#include <mavros_msgs/State.h>
#include <mavros_msgs/AttitudeTarget.h>
#include <ros/ros.h>
#include <tf/tf.h>
//#include <sensor_msgs/State.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/Range.h>
//#include <boost/asio.hpp>
////////////////////////////////////////
#include "protocol.h"
#include "serial.h"
#include "kbhit.h"
#include "common.h"
#include "MsgIo.h"
#include "quad2eular.h"
using namespace std;

#define simulator

MsgIo msg("/dev/ttyUSB0");

mavros_msgs::State current_state;
void state_cb(const mavros_msgs::State::ConstPtr& msg) { current_state = *msg; }

sensor_msgs::Imu imu_status;
void imu_state_cb(const sensor_msgs::Imu::ConstPtr& msg)
{
    // ROS_INFO("Imu Seq: [%d]", msg->header.seq);
    imu_status = *msg;
}

geometry_msgs::PoseStamped pos_status;
void pose_state_cb(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
    // ROS_INFO("test");
    pos_status = *msg;
}

sensor_msgs::Range dis_status;
void dis_state_cb(const sensor_msgs::Range::ConstPtr& msg)
{
    // ROS_INFO("test");
    dis_status = *msg;
}

geometry_msgs::PoseStamped xyzyaw2Position(float x, float y, float z, float yaw)
{
    geometry_msgs::PoseStamped pose;

    pose.pose.position.x = x;
    pose.pose.position.y = y;
    pose.pose.position.z = z;

    pose.pose.orientation = tf::createQuaternionMsgFromYaw(Deg2Reg(yaw));

    return pose;
}

void rawCtrl(float r, float p, float y, float thrust, ros::NodeHandle& nh)
{
    ros::Publisher local_pos_raw_pub = nh.advertise<mavros_msgs::AttitudeTarget>(
        "mavros/setpoint_raw/attitude", 10);

    mavros_msgs::AttitudeTarget msg;

    msg.thrust      = thrust;
    msg.orientation = tf::createQuaternionMsgFromRollPitchYaw(Deg2Reg(r), Deg2Reg(p), Deg2Reg(y));

    ROS_INFO("Raw Ctrl => r: [%f] p: [%f] y: [%f]h: [%f]", r, p, y, thrust);

    local_pos_raw_pub.publish(msg);
}

void waitRosConnect(ros::Rate& rate)
{
    ROS_INFO("connecting with mavros ...");
    while (ros::ok() && current_state.connected) {
        cout << "wait connect" << endl;
        ros::spinOnce();
        rate.sleep();
    }
    ROS_INFO("connected!");
}

void preOffboard(ros::Rate& rate, ros::Publisher& local_pos_pub)
{
    ROS_INFO("pre offboard...");
    // send a few setpoints before starting
    for (int i = 100; ros::ok() && i > 0; --i) {
        local_pos_pub.publish(xyzyaw2Position(0, 0, 0, 0));
        ros::spinOnce();
        rate.sleep();
    }
    ROS_INFO("pre offboard fished!");
}

void modeOffBoard(ros::NodeHandle& nh)
{
    ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>("mavros/set_mode");

    mavros_msgs::SetMode offb_set_mode;
    offb_set_mode.request.custom_mode = "OFFBOARD";

    if (set_mode_client.call(offb_set_mode) && offb_set_mode.response.success) {
        ROS_INFO("Offboard enabled");
    }
}

void modeLOITER(ros::NodeHandle& nh)
{
    if (current_state.mode != "AUTO.LOITER") {
        ros::ServiceClient set_mode_client = nh.serviceClient<mavros_msgs::SetMode>("mavros/set_mode");

        mavros_msgs::SetMode offb_set_mode;
        offb_set_mode.request.custom_mode = "AUTO.LOITER";

        if (set_mode_client.call(offb_set_mode) && offb_set_mode.response.success) {
            ROS_INFO("LOITER enabled");
        }
    }
}

void arm(ros::NodeHandle& nh)
{
    mavros_msgs::CommandBool arm_cmd;
    arm_cmd.request.value = true;

    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");

    if (arming_client.call(arm_cmd) && arm_cmd.response.success) {
        ROS_INFO("Vehicle armed");
    }
}

void disArm(ros::NodeHandle& nh)
{
    mavros_msgs::CommandBool arm_cmd;
    arm_cmd.request.value = false;

    ros::ServiceClient arming_client = nh.serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");

    if (arming_client.call(arm_cmd) && arm_cmd.response.success) {
        ROS_INFO("Vehicle disrmed");
    }
}

/*
void armAndModeOffboard(ros::NodeHandle& nh, ros::Time& last_request)
{
    // turn board into offboard mode
    if (current_state.mode != "OFFBOARD" && (ros::Time::now() - last_request > ros::Duration(5.0))) {
        modeOffBoard(nh);
        last_request = ros::Time::now();
    } else {
        // check system armd
        if (!current_state.armed && (ros::Time::now() - last_request > ros::Duration(5.0))) {
            arm(nh);
            last_request = ros::Time::now();
        }
    }
}
*/

void armAndModeOffboard(ros::NodeHandle& nh)
{
    // turn board into offboard mode
    if (current_state.mode != "OFFBOARD") {
        modeOffBoard(nh);
        // last_request = ros::Time::now();
    } else {
        // check system armd
        if (!current_state.armed) {
            arm(nh);
            // last_request = ros::Time::now();
        }
    }
}

void fallSafe(ros::NodeHandle& nh)
{
    //0.05
    if (ros::Time::now() - msg.Data.t > ros::Duration(1.0)) {
        modeLOITER(nh);
        //msg.sendStatusUseSerial(eular, current_state, pos_status, dis_status);
    } else {
        armAndModeOffboard(nh);
    }
}

char CtrlModeMsg[CtrlMode_max][10] = {
    "NoCtl",
    "Pose",
    "Raw"
};

void showInfo(geometry_msgs::Vector3 eular)
{
    cout << ((current_state.armed == true) ? "0" : "1");
    cout << '\t' << current_state.mode << endl;
    // show imu
    cout << ros::Time::now() << '\t' << endl;

    ROS_INFO("Ctrl Mode => [%s]",
        CtrlModeMsg[msg.Data.ctrlMode]);

    ROS_INFO("Set POSE => x: [%f], y: [%f], z: [%f]",
        msg.Data.setPos.x,
        msg.Data.setPos.y,
        msg.Data.setPos.z);

    ROS_INFO("Euler => x: [%f], y: [%f], z: [%f]",
        eular.x,
        eular.y,
        eular.z);

    ROS_INFO("POSE => x: [%f], y: [%f], z: [%f]",
        pos_status.pose.position.x,
        pos_status.pose.position.y,
        pos_status.pose.position.z);

    ROS_INFO("DIS => d: [%f]",
        dis_status.range);
}

int main(int argc, char** argv)
{
    ROS_INFO("System init ...");

    ros::init(argc, argv, "mavpi_node");
    // ros节点
    ros::NodeHandle nh;

// 锁机
#ifdef simulator
    disArm(nh);
#endif

    //订阅器
    ros::Subscriber state_sub = nh.subscribe<mavros_msgs::State>("mavros/state", 10, state_cb);
    ros::Subscriber imu_sub   = nh.subscribe<sensor_msgs::Imu>("mavros/imu/data", 10, imu_state_cb);
    ros::Subscriber pos_sub   = nh.subscribe<geometry_msgs::PoseStamped>(
        "mavros/local_position/pose", 10, pose_state_cb);
    ros::Subscriber dis_sub = nh.subscribe<sensor_msgs::Range>(
        "mavros/px4flow/ground_distance", 10, dis_state_cb);
    //发布器
    ros::Publisher local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>(
        "mavros/setpoint_position/local", 10);

    //
    // the setpoint publishing rate MUST be faster than 2Hz
    ros::Rate rate(20.0);
    //连接
    // wait for FCU connection
    waitRosConnect(rate);
    //准备offboard延时
    preOffboard(rate, local_pos_pub);

#ifdef simulator
    // offboard
    // modeOffBoard(nh);

    // modeLOITER(nh);
    // arm
    arm(nh);
#endif
    // log last request time
    ros::Time last_request = ros::Time::now();
    // clear serial buffer

    msg.clear();
    while (ros::ok()) {
        msg.checkSerialCmd();

        //fall safe
        fallSafe(nh);

        // set status led

        // pubpos
        if (msg.Data.ctrlMode == Pose) {
            local_pos_pub.publish(
                xyzyaw2Position(0, 0, 1.0, 0));
        } else if (msg.Data.ctrlMode == Raw) {
            ROS_WARN("Raw Ctl Not pass test!");
            rawCtrl(msg.Data.roll, msg.Data.pitch, msg.Data.setPos.w, msg.Data.setPos.z, nh);
        } else {
            ROS_WARN("Ctl Mode Err!");
        }
        // calc eular
        geometry_msgs::Vector3 eular = quad2eular(imu_status.orientation);

        // show state
        showInfo(eular);

        // pub to mcu
        msg.sendStatusUseSerial(eular, current_state, pos_status, dis_status);

        ros::spinOnce();
        rate.sleep();
    }

    return 0;
}
