#!/bin/bash

SCALE=$1

# Auto Scaling Group
echo -n "Deleting Auto Scaling Group (liblogcabin-ecs-demo-group) .. "
# Save Auto Scaling Group instances to wait for them to terminate
INSTANCE_IDS=$(aws autoscaling describe-auto-scaling-groups --auto-scaling-group-names liblogcabin-ecs-demo-group --query 'AutoScalingGroups[0].Instances[*].InstanceId' --output text)
aws autoscaling delete-auto-scaling-group --force-delete --auto-scaling-group-name liblogcabin-ecs-demo-group
echo $INSTANCE_IDS
echo "done"

status_string=""
for i in $(seq 1 $SCALE); do
    status_string="$status_string terminated"
done

status_string=${status_string:1}

# Wait for instances to terminate
echo -n "Waiting for instances to terminate (this may take a few minutes) .. "
STATE="foo"
while [ -n "$STATE" -a "$STATE" != "$status_string" ]; do
    STATE=$(aws ec2 describe-instances --instance-ids ${INSTANCE_IDS} --query 'Reservations[0].Instances[*].State.Name' --output text)
    # Remove spacing
    STATE=$(echo $STATE)
    sleep 2
done
echo "done"

# Launch configuration
echo -n "Deleting Launch Configuration (liblogcabin-ecs-launch-configuration) .. "
aws autoscaling delete-launch-configuration --launch-configuration-name liblogcabin-ecs-launch-configuration
echo "done"

# IAM role
echo -n "Deleting liblogcabin-ecs-role IAM role (liblogcabin-ecs-role) .. "
aws iam remove-role-from-instance-profile --instance-profile-name liblogcabin-ecs-instance-profile --role-name liblogcabin-ecs-role
aws iam delete-instance-profile --instance-profile-name liblogcabin-ecs-instance-profile
aws iam delete-role-policy --role-name liblogcabin-ecs-role --policy-name liblogcabin-ecs-policy
aws iam delete-role --role-name liblogcabin-ecs-role
echo "done"


# Key pair
echo -n "Deleting Key Pair (liblogcabin-ecs-demo-key, deleting file liblogcabin-ecs-demo-key.pem) .. "
aws ec2 delete-key-pair --key-name liblogcabin-ecs-demo-key
rm -f liblogcabin-ecs-demo-key.pem
echo "done"

# Security group
echo -n "Deleting Security Group (liblogcabin-ecs-demo) .. "
for group_id in $(aws ec2 describe-security-groups --query 'SecurityGroups[?GroupName==`liblogcabin-ecs-demo`].GroupId' --output text); do
    aws ec2 delete-security-group --group-id $group_id
done
echo "done"

# Internet Gateway
echo -n "Deleting Internet gateway .. "
VPC_ID=$(aws ec2 describe-tags --filters Name=resource-type,Values=vpc,Name=tag:Name,Values=liblogcabin-ecs-demo-vpc --query 'Tags[0].ResourceId' --output text)
GW_ID=$(aws ec2 describe-tags --filters Name=resource-type,Values=internet-gateway,Name=tag:Name,Values=liblogcabin-ecs-demo --query 'Tags[0].ResourceId' --output text)
aws ec2 detach-internet-gateway --internet-gateway-id $GW_ID --vpc-id $VPC_ID
aws ec2 delete-internet-gateway --internet-gateway-id $GW_ID
echo "done"

# Subnet
echo -n "Deleting Subnet (liblogcabin-ecs-demo-subnet) .. "
SUBNET_ID=$(aws ec2 describe-tags --filters Name=resource-type,Values=subnet,Name=tag:Name,Values=liblogcabin-ecs-demo-subnet --query 'Tags[0].ResourceId' --output text)
aws ec2 delete-subnet --subnet-id $SUBNET_ID
echo "done"

# VPC
echo -n "Deleting VPC (liblogcabin-ecs-demo-vpc) .. "
aws ec2 delete-vpc --vpc-id $VPC_ID
echo "done"

# Cluster
echo -n "Deleting ECS cluster (liblogcabin-ecs-demo-cluster) .. "
aws ecs delete-cluster --cluster liblogcabin-ecs-demo-cluster > /dev/null
echo "done"
