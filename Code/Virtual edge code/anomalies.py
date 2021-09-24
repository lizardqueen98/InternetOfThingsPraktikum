import json
import time
import math
import random
import statsmodels
import pandas as pd
from datetime import datetime

from scipy.stats import norm
import statsmodels.api as sm
## for plot of models 
import matplotlib.pyplot as plt 
## used for performing ADF test to see if time series is stationary
from statsmodels.tsa.stattools import adfuller
from pmdarima.arima import ADFTest
## used in the part where RSME cost function is computed
from math import sqrt
from sklearn.metrics import mean_squared_error
## the below is needed for AR models 
from statsmodels.tsa.ar_model import AutoReg
## the below is needed for MA, ARMA, and ARIMA models
from statsmodels.tsa.arima.model import ARIMA
import numpy as np

import pmdarima as pm
from pmdarima import auto_arima
#ignore harmless warning with code below
import warnings

import http.client
from time import time
from time import mktime
import ssl

#from minio import Minio
import pickle
import io

from sklearn.cluster import KMeans
from sklearn.preprocessing import StandardScaler
from sklearn.ensemble import IsolationForest
from sklearn.svm import OneClassSVM

import optuna
from sklearn.metrics import f1_score
from sklearn.metrics import precision_score
from sklearn.metrics import accuracy_score

import paho.mqtt.client as mqtt

CONSUMER_URL = 'iotplatform.caps.in.tum.de:443'
DEV_JWT = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA5MTQ2OTMsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfODMifQ.mB9292Db6piVAOIcDnOKO5VFO6qWXmLNWuKOKrRN8FIjMnnfyO0XNOr9k2YjtDhHVyo8JI1igbDht8BRsre1zeM6JIsIwCpNZ9polPFb93hHf04xHyn0vsWw5JO2YeY4-ifyX-WifIgpwWfJvbaqZn_nY2P_thmqpH9tNKEOAkZVf3ovV_9WypzYH7z_mCfId0TI9W96L3LJtU6HEorT4A1ft5XXbZxgQZudzENoYg3J2UvFwtI42phMGbor4i9LrHCd9y6uv46s78-qnUgEWpd3qL91wdtJT0JFgkUlc8BwXyPIwJIQWb0-AR090Upv59aIkmIqig4LGO1ogX49eHcasw0Rzp1nT8aBWBY8lJjsZ3bk-rH2EYelr5RARK2q_zKQjDt9mFWTeERyeVmbpkEyur4hwbnse0zGee9hUgair3LCs-2AWYqCkXGqDjFzcAuuLCSCQtXqXw-hENtcnCo8_q_F9G_KzSNqRPZ1VViA_3a-0I7EALzNGsGdrUk_KCaVY2sXmBOX3Px4jVGxxE-IDJPK723DL1dQgHR_IICyNT-wdbaZMSU5GyevMOrCRyKEXnvlSWakXqJagdmw5JVRivcdJ8aNnMMZgllbXYW7wJDTZYwMeyZjqIBkNBfsVuhGGjknsugW4FUZnoqwXS3QuUCgtD7pZt4Kaz_9CZI'

sensorID = 1248
batchSize = 100
searchPath = '/api/consumers/consume/' + str(sensorID) + '/_search?' #base path - search all
countPath = '/api/consumers/consume/' + str(sensorID) + '/_count?' #base path - count all

generated_ts = pd.DataFrame(columns = ['time', 'count'])

# Get the data 
consumerConn = http.client.HTTPSConnection(CONSUMER_URL,context = ssl._create_unverified_context())
consumerConn.connect()

scroll_id = ''
while True:
    # Slicing our requests
    if scroll_id == '':
        searchPath = searchPath + '&scroll=3m&size=' + str(batchSize)
        consumerConn.request('GET', searchPath, '', {  "Content-Type": "application/x-www-form-urlencoded", "Authorization": "Bearer " + DEV_JWT })
    else:
        searchPath = '/api/consumers/consume/' + str(sensorID) + '/_search/scroll?size=' + str(batchSize)
        consumerConn.request('GET', searchPath, '{"scroll":"3m","scroll_id":"' + scroll_id + '"}',
                                {  "Content-Type": "application/json", "Authorization": "Bearer " + DEV_JWT })
    iotPlResp = consumerConn.getresponse()
    rawData = iotPlResp.read()
    respData = json.loads(rawData)
    respData = respData["body"]
    #print(scroll_id)
    if scroll_id == '':
        scroll_id = respData["_scroll_id"]
    observationsArray = respData["hits"]["hits"]

    if len(observationsArray) == 0:
        break

    for observation in observationsArray:
        timestamp_s = int(observation['_source']['timestamp'] / 1000)
        count = int(observation['_source']['value'])
        cur_date = datetime.fromtimestamp(timestamp_s)
        df_row = pd.DataFrame([[cur_date, count]], columns=['time', 'count'])
        generated_ts = generated_ts.append(df_row)

consumerConn.close()

generated_ts.index = generated_ts.time
generated_ts.index.name = 'index'

generated_ts = generated_ts.sort_index()
generated_ts["numOfObs"] = range(0, len(generated_ts))

print(len(generated_ts))

data = generated_ts['count']
# calculate mean
mean = np.mean(data) 
# calculate standard deviation
sd = np.std(data)
# determine a threhold
threshold = 3
# create empty list to store outliers
outliers = []
# detect outlier
for i in data: 
    z = (i-mean)/sd # calculate z-score
    if abs(z) > threshold:  # identify outliers
        outliers.append(i) # add to the empty list
# print outliers    
#print("The detected outliers are: ", outliers)

df = generated_ts

df = df.sort_values('time')
df['time_int'] = df.time.astype(np.int64)
df["anomaly"] = df['count'].isin(outliers)
#print(df)

#plotting not included in vm code

#fig, ax = plt.subplots(figsize=(10,6))
#a = df[df['count'].isin(outliers)] #anomaly

#ax.plot(df['time_int'], df['count'], color='blue', label='Normal')
#ax.scatter(a['time_int'], a['count'], color='red', label='Anomaly')
#plt.xlabel('Date Time Integer')
#plt.ylabel('Count')
#plt.legend()
#plt.show()

#generated_ts.plot(x='time', y='count', figsize=(12,6))
#plt.xlabel('Date time')
#plt.ylabel('Count')
#plt.title('Time Series of room count by date time')

#elbow method for cluster size
data = generated_ts[['count']]
n_cluster = range(1, 20)
kmeans = [KMeans(n_clusters=i).fit(data) for i in n_cluster]
scores = [kmeans[i].score(data) for i in range(len(kmeans))]

#plotting elbow curve
#fig, ax = plt.subplots(figsize=(10,6))
#ax.plot(n_cluster, scores)
#plt.xlabel('Number of Clusters')
#plt.ylabel('Score')
#plt.title('Elbow Curve')
#plt.show()

def getDistanceByPoint(data, model):
    distance = pd.Series()
    for i in range(0, len(data)):
        Xa = np.array(data.iloc[i])
        Xb = model.cluster_centers_[model.labels_[i]-1]
        #distance.set_value(i, np.linalg.norm(Xa-Xb))
        #distance[i] = np.linalg.norm(Xa-Xb)
        distance = distance.append(pd.Series([np.linalg.norm(Xa-Xb)], index=[i]))
    return distance

outliers_fraction = len(outliers)/len(df)
print(outliers_fraction)
# get the distance between each point and its nearest centroid. The biggest distances are considered as anomaly
distance = getDistanceByPoint(data, kmeans[5])
number_of_outliers = int(outliers_fraction*len(distance))
threshold = distance.nlargest(number_of_outliers).min()
# anomaly1 contain the anomaly result of the above method Cluster (0:normal, 1:anomaly) 
anomaly = list(distance >= threshold)
df['anomaly1'] = anomaly

#print(anomaly)
#print(type(anomaly))
#print(distance)
#print(number_of_outliers)
#print(threshold)
#print(df)
#print(distance >= threshold)

df = df.sort_values('time')
df['time_int'] = df.time.astype(np.int64)

#plotting
#fig, ax = plt.subplots(figsize=(16, 6))
#a = df.loc[df['anomaly1'] == True, ['time_int', 'count']] #anomaly

#ax.plot(df['time_int'], df['count'], color='blue', label='Normal')
#ax.scatter(a['time_int'], a['count'], color='red', label='Anomaly')
#plt.xlabel('Date Time Integer')
#plt.ylabel('Count')
#plt.legend()
#plt.show()

data = df[['count']]
scaler = StandardScaler()
np_scaled = scaler.fit_transform(data)
data = pd.DataFrame(np_scaled)

def objective(trial: optuna.Trial) -> float:
    kwargs = {}
    kwargs["n_estimators"] = trial.suggest_int("n_estimators", 10, 300, 15)
    kwargs["max_samples"] = trial.suggest_int("max_samples", 10, 300, 15)
    kwargs["contamination"] = trial.suggest_uniform("contamination", 0.0, 0.1)
    #kwargs["max_features"] = trial.suggest_uniform("max_features", 0.0, 1.0)
    kwargs["bootstrap"] = trial.suggest_categorical("bootstrap", [True, False])
    kwargs["warm_start"] = trial.suggest_categorical("warm_start", [True, False])
    model = IsolationForest(**kwargs)
    model.fit(data)
    anomaly = pd.Series(model.predict(data), index = df.index)
    anomaly.replace({-1:1, 1:0}, inplace=True)

    #print(f1_score(df["anomaly1"].to_numpy(), anomaly.to_numpy().astype(bool)))
    #print(precision_score(df["anomaly1"].to_numpy(), anomaly.to_numpy().astype(bool)))
    #print(accuracy_score(df["anomaly1"].to_numpy(), anomaly.to_numpy().astype(bool)))
    return f1_score(df["anomaly"].to_numpy(), anomaly.to_numpy().astype(bool))

# train isolation forest
#model =  IsolationForest(contamination=outliers_fraction)
#model.fit(data) 
#df['anomaly2'] = list(model.predict(data))

#print(model.predict(data))
#print(df)

study = optuna.create_study(direction = "maximize")
study.optimize(objective, n_trials = 20)
model = IsolationForest(**study.best_params)
model.fit(data)

df['anomaly2'] = pd.Series(model.predict(data), index = df.index)
df['anomaly2'].replace({-1:1, 1:0}, inplace=True)

# visualization
#fig, ax = plt.subplots(figsize=(16,6))

#a = df.loc[df['anomaly2'] == 1, ['time_int', 'count']] #anomaly

#ax.plot(df['time_int'], df['count'], color='blue', label = 'Normal')
#ax.scatter(a['time_int'],a['count'], color='red', label = 'Anomaly')
#plt.legend()
#plt.show()

#publishing anomalies
client = mqtt.Client("platform")
client.username_pw_set(username="JWT", password="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA4NzI2ODUsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfMTA3In0.x40nxuvC7xAGbXsuM0HsxE_uzya3oGFfu-pcI09e0e4h_tIuDn6em0NxyAmDjbjECKpQPoCuTlam9BRV8VSZkv3YwYNNATxesQJQ0XYHA9QUTdXzSZgefiLTE00nSq9gPD6O2dddwx6egAr9xcPqlOC6WXOU9mb1pnFoOv0BMymLvsSJndsB4bxMxi3CYrMirFRxFzPaGFZvwRNMmZZ5oGWtLhtLJ7cJ0t0wdD9jV1334AQfCayQvb9n7_6E3ruSZjfUdGMj5jV9lFaChfKjzs2yq15dDfKbbfOkDGwLPrpURH3Y65ycuPyaUn6TTz_EwUF2d5Ai5i2x5cJn_TSQ2vbDJw3BZxcBZ7NxiXoG00OwQ02QXMAryyYJ1SKdr_BPxyy2C2XeZFGg3ip42bUkn92tScFiQtzUp-WaQSFWiZK3D1sdJglPb6l1iEQCI7HfNk0g0ADlvKh_aGTcxy4sjFM69phDfj_UV7T5Of-BV1lk7mifC2tdg1rqqttCMQ9uNbnsrw6YcfsO896zr7uFDaVsG98veUCXQYtuXkbn3XWqxGUM13H6nb2ThlZn2dMxi5EsWKXktyQDtAmB_Sf4akU6MU4uW-d8QpzTv9wPaW2Qo1KagA6KIcdM9AjT9J8BsLd8kzMyuUIpVoioT5REp3kIRACzcLLiY6YxyXCy22U")
client.connect("131.159.35.132", 1883)

print(df)

for i in range(len(df)):
    if(df['anomaly2'].iloc[i] == 1):
        payload = {
            "username": "group2_2021_ss",
            "anomalies": df['count'].iloc[i],
            "device_id": 107,
            "timestamp": int(df['time'].iloc[i].to_pydatetime().timestamp() * 1000)
            }
        client.publish("50_107", json.dumps(payload))
        print("Just published " + str(payload) + " to topic 50_107")

data = df[['count']]
scaler = StandardScaler()
np_scaled = scaler.fit_transform(data)
data = pd.DataFrame(np_scaled)

# train oneclassSVM 
model = OneClassSVM(nu=outliers_fraction, kernel="rbf", gamma=0.1*10e-5)
model.fit(data)
df['anomaly3'] = list(model.predict(data))

#plotting
#fig, ax = plt.subplots(figsize=(16,6))
#a = df.loc[df['anomaly3'] == -1, ['time_int', 'count']] #anomaly

#ax.plot(df['time_int'], df['count'], color='blue')
#ax.scatter(a['time_int'],a['count'], color='red')
#plt.show()