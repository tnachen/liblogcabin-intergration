#!/bin/bash

REGION=$(aws configure list 2> /dev/null | grep region | awk '{ print $2 }')
if [ -z "$REGION" ]; then
    echo "error: Region not set, please make sure to run 'aws configure'"
    exit 1
fi

AMI=`curl https://gist.githubusercontent.com/tnachen/1c263911e36a55501f34202a93b0503a/raw/d913d1e4351a85a0804d9fa5f21bd6386c352a53/gistfile1.txt | grep $REGION | cut -d' ' -f2`

if [ -z "$AMI" ]; then
    echo "Unable to find a ECS AMI for region $REGION"
    exit 1
fi

SCALE=$1

set -euo pipefail

# Cluster
echo -n "Creating ECS cluster (liblogcabin-ecs-demo-cluster) .. "
aws ecs create-cluster --cluster-name liblogcabin-ecs-demo-cluster > /dev/null
echo "done"

# VPC
echo -n "Creating VPC (liblogcabin-ecs-demo-vpc) .. "
VPC_ID=$(aws ec2 create-vpc --cidr-block 172.31.0.0/28 --query 'Vpc.VpcId' --output text)
aws ec2 modify-vpc-attribute --vpc-id $VPC_ID --enable-dns-support
aws ec2 modify-vpc-attribute --vpc-id $VPC_ID --enable-dns-hostnames
# tag it for later deletion
aws ec2 create-tags --resources $VPC_ID --tag Key=Name,Value=liblogcabin-ecs-demo-vpc
echo "done"

# Subnet
echo -n "Creating Subnet (liblogcabin-ecs-demo-subnet) .. "
SUBNET_ID=$(aws ec2 create-subnet --vpc-id $VPC_ID --cidr-block 172.31.0.0/28 --query 'Subnet.SubnetId' --output text)
# tag it for later deletion
aws ec2 create-tags --resources $SUBNET_ID --tag Key=Name,Value=liblogcabin-ecs-demo-subnet
echo "done"

# Internet Gateway
echo -n "Creating Internet Gateway (liblogcabin-ecs-demo) .. "
GW_ID=$(aws ec2 create-internet-gateway --query 'InternetGateway.InternetGatewayId' --output text)
# tag it for later deletion
aws ec2 create-tags --resources $GW_ID --tag Key=Name,Value=liblogcabin-ecs-demo
aws ec2 attach-internet-gateway --internet-gateway-id $GW_ID --vpc-id $VPC_ID
TABLE_ID=$(aws ec2 describe-route-tables --query 'RouteTables[?VpcId==`'$VPC_ID'`].RouteTableId' --output text)
aws ec2 create-route --route-table-id $TABLE_ID --destination-cidr-block 0.0.0.0/0 --gateway-id $GW_ID > /dev/null
echo "done"

# Security group
echo -n "Creating Security Group (liblogcabin-ecs-demo) .. "
SECURITY_GROUP_ID=$(aws ec2 create-security-group --group-name liblogcabin-ecs-demo --vpc-id $VPC_ID --description 'Liblogcabin ECS Demo' --query 'GroupId' --output text)
# Wait for the group to get associated with the VPC
sleep 5
aws ec2 authorize-security-group-ingress --group-id $SECURITY_GROUP_ID --protocol tcp --port 22 --cidr 0.0.0.0/0
aws ec2 authorize-security-group-ingress --group-id $SECURITY_GROUP_ID --protocol tcp --port 80 --cidr 0.0.0.0/0

for i in $(seq 1 $SCALE)
do
    # HTTP server port
    aws ec2 authorize-security-group-ingress --group-id $SECURITY_GROUP_ID --protocol tcp --port 1230$i --source-group $SECURITY_GROUP_ID
    # Raft port
    aws ec2 authorize-security-group-ingress --group-id $SECURITY_GROUP_ID --protocol tcp --port 600$i --source-group $SECURITY_GROUP_ID
done

echo "done"

# Key pair
echo -n "Creating Key Pair (liblogcabin-ecs-demo, file liblogcabin-ecs-demo-key.pem) .. "
aws ec2 create-key-pair --key-name liblogcabin-ecs-demo-key --query 'KeyMaterial' --output text > liblogcabin-ecs-demo-key.pem
chmod 600 liblogcabin-ecs-demo-key.pem
echo "done"

# IAM role
echo -n "Creating IAM role (liblogcabin-ecs-role) .. "
aws iam create-role --role-name liblogcabin-ecs-role --assume-role-policy-document file://data/liblogcabin-ecs-role.json > /dev/null
aws iam put-role-policy --role-name liblogcabin-ecs-role --policy-name liblogcabin-ecs-policy --policy-document file://data/liblogcabin-ecs-policy.json
aws iam create-instance-profile --instance-profile-name liblogcabin-ecs-instance-profile > /dev/null
# Wait for the instance profile to be ready, otherwise we get an error when trying to use it
while ! aws iam get-instance-profile --instance-profile-name liblogcabin-ecs-instance-profile  2>&1 > /dev/null; do
    sleep 2
done

aws iam add-role-to-instance-profile --instance-profile-name liblogcabin-ecs-instance-profile --role-name liblogcabin-ecs-role
echo "done"

# Launch configuration
echo -n "Creating Launch Configuration (liblogcabin-ecs-launch-configuration) .. "

sleep 15

TMP_USER_DATA_FILE=$(mktemp /tmp/liblogcabin-ecs-demo-user-data-XXXX)
trap 'rm $TMP_USER_DATA_FILE' EXIT
cp data/set-ecs-cluster-name.sh $TMP_USER_DATA_FILE
aws autoscaling create-launch-configuration --image-id $AMI --launch-configuration-name liblogcabin-ecs-launch-configuration --key-name liblogcabin-ecs-demo-key --security-groups $SECURITY_GROUP_ID --instance-type t2.medium --user-data file://$TMP_USER_DATA_FILE  --iam-instance-profile liblogcabin-ecs-instance-profile --associate-public-ip-address --instance-monitoring Enabled=false
echo "done"

# Auto Scaling Group

echo -n "Creating Auto Scaling Group (liblogcabin-ecs-demo-group) with $SCALE instances .. "
aws autoscaling create-auto-scaling-group --auto-scaling-group-name liblogcabin-ecs-demo-group --launch-configuration-name liblogcabin-ecs-launch-configuration --min-size $SCALE --max-size $SCALE --desired-capacity $SCALE --vpc-zone-identifier $SUBNET_ID

# Wait for instances to join the cluster
echo -n "Waiting for instances to join the cluster (this may take a few minutes) .. "
while [ "$(aws ecs describe-clusters --clusters liblogcabin-ecs-demo-cluster --query 'clusters[0].registeredContainerInstancesCount' --output text)" != $SCALE ]; do
    sleep 6
done
echo "done"
