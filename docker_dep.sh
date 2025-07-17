IMAGE_NAME=$1
crictl rmi docker.io/library/$IMAGE_NAME
docker rmi -f $IMAGE_NAME 
source ~/.bash_profile && cd /root/shardora && sh build.sh a Debug && sh build.sh a Release && docker build -t $IMAGE_NAME:latest . && rm -rf $IMAGE_NAME.tar && docker save -o $IMAGE_NAME.tar $IMAGE_NAME&& ctr -n k8s.io images import $IMAGE_NAME.tar

kubectl scale deployment $IMAGE_NAME-dep --replicas=0 
kubectl delete pods -l app=$IMAGE_NAME-app 

rm -rf /var/dep_pod_temp_$IMAGE_NAME.yaml /var/dep_svr_temp_$IMAGE_NAME.yaml 
cp -rf dep_pod_temp.yaml  /var/dep_pod_temp_$IMAGE_NAME.yaml
cp -rf dep_svr_temp.yaml  /var/dep_svr_temp_$IMAGE_NAME.yaml
sed -i 's/REPLACE_IMAGE_NAME/'$IMAGE_NAME'/g' /var/dep_pod_temp_$IMAGE_NAME.yaml 
kubectl apply -f /var/dep_pod_temp_$IMAGE_NAME.yaml 
 
kubectl delete service $IMAGE_NAME-service 
sed -i 's/REPLACE_IMAGE_NAME/'$IMAGE_NAME'/g' /var/dep_svr_temp_$IMAGE_NAME.yaml 
sed -i 's/REPLACE_SERVICE_PORT/'$2'/g' /var/dep_svr_temp_$IMAGE_NAME.yaml 
kubectl apply -f /var/dep_svr_temp_$IMAGE_NAME.yaml 
 
kubectl get services 
kubectl get pods | grep $IMAGE_NAME

sleep 5
pod_name=`kubectl get pods | grep $IMAGE_NAME | awk -F' ' '{print $1}'`
echo $pod_name
kubectl exec -it $pod_name  -- /bin/bash