#!/usr/bin/python3

from minio import Minio
import pandas as pd
import pickle
import io
import os
from time import time
import paho.mqtt.client as mqtt
import json
from kafka import KafkaProducer

import time
import math
import random
import statsmodels
from datetime import datetime

from scipy.stats import norm
import statsmodels.api as sm
## for plot of models
import matplotlib.pyplot as plt
## used for performing ADF test to see if time series is stationary
from statsmodels.tsa.stattools import adfuller
## used in the part where RSME cost function is computed
from math import sqrt
## the below is needed for AR models
from statsmodels.tsa.ar_model import AutoReg
## the below is needed for MA, ARMA, and ARIMA models
from statsmodels.tsa.arima.model import ARIMA
import numpy as np

#ignore harmless warning with code below
import warnings

import http.client
from time import time
from time import mktime
import ssl
from time import sleep
from sklearn.preprocessing import MinMaxScaler

def create_dataset(dataset, look_back=1):
        dataX, dataY = [], []
        for i in range(len(dataset)-look_back-1):
                a = dataset[i:(i+look_back), 0]
                dataX.append(a)
                dataY.append(dataset[i + look_back, 0])
        return np.array(dataX), np.array(dataY)

def dateTimeToSeconds(someTimeStampVariable):
    return mktime(someTimeStampVariable.to_pydatetime().timetuple())

def timeBetweenTwoObservationsInSeconds(obs1Time, obs2Time):
    return dateTimeToSeconds(obs2Time) - dateTimeToSeconds(obs1Time)

def findSmallestTimeBetweenTwoObservationsAndRemoveDuplicates(timeSeriesDataset):
    newTimeSeriesDataset = timeSeriesDataset.copy(deep=True)

    ##making the index column unique, so we can delete a single observation
    newTimeSeriesDataset = newTimeSeriesDataset.sort_index()
    newTimeSeriesDataset.index = range(0, len(newTimeSeriesDataset))
    newTimeSeriesDataset.index.name = 'index'

    count_i = 0
    count_j = 0
    countOfDuplicateObservations = 0
    minBetweenTwoObservations = 100000 #given in seconds between two timestamps
    if(len(newTimeSeriesDataset) > 2):
        minBetweenTwoObservations = ( dateTimeToSeconds(newTimeSeriesDataset["time"][1]) -
                                        dateTimeToSeconds(newTimeSeriesDataset["time"][0]) )
        print(minBetweenTwoObservations)

    for i in newTimeSeriesDataset.index[:]:
        if((count_i + 1) < len(newTimeSeriesDataset)):

            timeBetweenTwoObservations = timeBetweenTwoObservationsInSeconds(newTimeSeriesDataset["time"][count_i],
                                            newTimeSeriesDataset["time"][count_i+1])

            if( timeBetweenTwoObservations == 0 and
                   (newTimeSeriesDataset["count"][count_i] - newTimeSeriesDataset["count"][count_i+1] ) < 0.0001):
                print("Zero seconds between to observations:", timeBetweenTwoObservations, " is between ",  count_i,
                          " and ", count_i+1)
                print("\t newTimeSeriesDataset['time'][count_i]=", newTimeSeriesDataset["time"][count_i],
                          "\n\t newTimeSeriesDataset['time'][count_i+1]=", newTimeSeriesDataset["time"][count_i+1])
                print("\t newTimeSeriesDataset['count'][count_i]=", newTimeSeriesDataset["count"][count_i],
                          "\n\t newTimeSeriesDataset['count'][count_i+1]=", newTimeSeriesDataset["count"][count_i+1])
                newTimeSeriesDataset.drop([i], inplace=True)
                countOfDuplicateObservations += 1
            elif( timeBetweenTwoObservations < minBetweenTwoObservations):
                minBetweenTwoObservations = timeBetweenTwoObservations
                print("Minimal time between two observations", minBetweenTwoObservations, " is between ",  count_i,
                      " and ", count_i+1)

        count_i = count_i + 1

    print("The smallest time between two observations is: ", minBetweenTwoObservations)
    print("len(newTimeSeriesDataset)=", len(newTimeSeriesDataset), "; len(timeSeriesDataset)=",len(timeSeriesDataset))
    print("countOfDuplicateObservations=", countOfDuplicateObservations)
    newTimeSeriesDataset["numOfObs"] = range(0, len(newTimeSeriesDataset))
    newTimeSeriesDataset.index = newTimeSeriesDataset.time
    newTimeSeriesDataset.index.name = "index"
    #printFullTimeSeries(newTimeSeriesDataset)

    return (newTimeSeriesDataset, minBetweenTwoObservations)

def publish(count, timestamp):
#function for bublishing count of ppl called every 5 mins
    producer = KafkaProducer(bootstrap_servers='localhost:9092')
    producer.send('predictions', json.dumps(payload).encode('utf-8'))
    print("Just published " + str(payload) + " to topic predictions")

# we read all three models
f = open("/home/ubuntu/week7task/arima_model", 'rb')
arima_model = pd.read_pickle(f)

f = open("/home/ubuntu/week7task/lstm_model", 'rb')
lstm_model = pd.read_pickle(f)

f = open("/home/ubuntu/week7task/prop_model", 'rb')
prop_model = pd.read_pickle(f)

with open('/home/ubuntu/week7task/count.txt') as f:
        countFile = f.readlines()

print(countFile[0])
countFile = int(countFile[0])

#predict next value
forecast = arima_model.forecast(countFile)

timestamp = int(forecast.index[countFile - 1].to_pydatetime().timestamp() * 1000)
count = int(forecast.values[countFile - 1])

if count < 0:
    count = 0

print(count)
print(timestamp)

#write that value to a file
with open('/home/ubuntu/week7task/arima_forecasts.txt', 'a') as f:
    f.write(str(timestamp) + " " + str(count) + "\n")

# we predict value with the prop
future = prop_model.make_future_dataframe(freq='15min', periods=countFile)
future["floor"] = 0
future["cap"] = 45
future = future[~(future["ds"].isna())]
print(future)

forcastsPropeth = prop_model.predict(future)

timestamp = int(forcastsPropeth.ds[len(forcastsPropeth) - 1].to_pydatetime().timestamp() * 1000)
print(timestamp)

count = int(forcastsPropeth.yhat[len(forcastsPropeth) - 1])
print(count)

#we write that value to a file
with open('/home/ubuntu/week7task/prop_forecasts.txt', 'a') as f:
    f.write(str(timestamp) + " " + str(count) + "\n")

#getting the data for lstm 
CONSUMER_URL = 'iotplatform.caps.in.tum.de:443'
DEV_JWT = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA5MTQ2OTMsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfODMifQ.mB9292Db6piVAOIcDnOKO5VFO6qWXmLNWuKOKrRN8FIjMnnfyO0XNOr9k2YjtDhHVyo8JI1igbDht8BRsre1zeM6JIsIwCpNZ9polPFb93hHf04xHyn0vsWw5JO2YeY4-ifyX-WifIgpwWfJvbaqZn_nY2P_thmqpH9tNKEOAkZVf3ovV_9WypzYH7z_mCfId0TI9W96L3LJtU6HEorT4A1ft5XXbZxgQZudzENoYg3J2UvFwtI42phMGbor4i9LrHCd9y6uv46s78-qnUgEWpd3qL91wdtJT0JFgkUlc8BwXyPIwJIQWb0-AR090Upv59aIkmIqig4LGO1ogX49eHcasw0Rzp1nT8aBWBY8lJjsZ3bk-rH2EYelr5RARK2q_zKQjDt9mFWTeERyeVmbpkEyur4hwbnse0zGee9hUgair3LCs-2AWYqCkXGqDjFzcAuuLCSCQtXqXw-hENtcnCo8_q_F9G_KzSNqRPZ1VViA_3a-0I7EALzNGsGdrUk_KCaVY2sXmBOX3Px4jVGxxE-IDJPK723DL1dQgHR_IICyNT-wdbaZMSU5GyevMOrCRyKEXnvlSWakXqJagdmw5JVRivcdJ8aNnMMZgllbXYW7wJDTZYwMeyZjqIBkNBfsVuhGGjknsugW4FUZnoqwXS3QuUCgtD7pZt4Kaz_9CZI'

sensorID = 1248
batchSize = 100
searchPath = '/api/consumers/consume/' + str(sensorID) + '/_search?' #base path - search all
countPath = '/api/consumers/consume/' + str(sensorID) + '/_count?' #base path - count all

generated_ts = pd.DataFrame(columns = ['time', 'count'])

ts = time()
curTs = int(ts - (ts % 60))
weekAgoTs = int(curTs - 14 * 24 * 60 * 60)
sQuery = 'q=timestamp:[' + str(weekAgoTs*1000) + '%20TO%20' + str(curTs*1000) + ']'
searchPath = searchPath + sQuery
countPath = countPath + sQuery

# Get the data
consumerConn = http.client.HTTPSConnection(CONSUMER_URL,context = ssl._create_unverified_context())
consumerConn.connect()

scroll_id = ''
while True:
    # Slicing requests
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

print(generated_ts)

generated_ts, minTimeBetweenTwoObservations = findSmallestTimeBetweenTwoObservationsAndRemoveDuplicates(generated_ts)

resampled_15min_df = generated_ts.resample('15T').pad() # Resample the data to 15 minutes interval and forward filling
resampled_15min_df["time"] = resampled_15min_df.index
resampled_15min_df["numOfObs"] = range(0, len(resampled_15min_df))

resampled_15min_df = resampled_15min_df.bfill()
print(resampled_15min_df)

timestamp = int(resampled_15min_df.index[len(resampled_15min_df) - 1].to_pydatetime().timestamp() * 1000)
print(timestamp)
timestamp = timestamp + 900000
print(timestamp)

dataset = resampled_15min_df['count']
dataset = dataset.values
dataset = dataset.astype('float32')
dataset = dataset.reshape(-1, 1)

scaler = MinMaxScaler(feature_range=(0, 1))
dataset = scaler.fit_transform(dataset)

look_back = 4*7*24

datasetX, datasetY = create_dataset(dataset, look_back)

# reshape input to be [samples, time steps, features]
datasetX = np.reshape(datasetX, (datasetX.shape[0], 1, datasetX.shape[1]))

#predict value with lstm
lstmPredict = lstm_model().predict(datasetX)

lstmPredict = scaler.inverse_transform(lstmPredict)

count = int(lstmPredict[0])

if count < 0:
    count = 0

print(timestamp)
print(count)

#write value to file
with open('/home/ubuntu/week7task/lstm_forecasts.txt', 'a') as f:
    f.write(str(timestamp) + " " + str(count) + "\n")

with open('/home/ubuntu/week7task/count.txt', 'w') as f:
    f.write(str(countFile + 1))
