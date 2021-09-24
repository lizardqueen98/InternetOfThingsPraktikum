#!/usr/bin/python3

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
## used in the part where RSME cost function is computed
from math import sqrt
from sklearn.metrics import mean_squared_error
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

from minio import Minio
import pickle
import io
import paho.mqtt.client as mqtt

EPSILON = 1e-10

def _percentage_error(actual: np.ndarray, predicted: np.ndarray):
    """
    Percentage error
    Note: result is NOT multiplied by 100
    """
    return _error(actual, predicted) / (actual + EPSILON)

def mape(actual: np.ndarray, predicted: np.ndarray):
    """
    Mean Absolute Percentage Error
    Properties:
        + Easy to interpret
        + Scale independent
        - Biased, not symmetric
        - Undefined when actual[t] == 0
    Note: result is NOT multiplied by 100
    """
    return np.mean(np.abs(_percentage_error(actual, predicted)))

def smape(actual: np.ndarray, predicted: np.ndarray):
    """
    Symmetric Mean Absolute Percentage Error
    Note: result is NOT multiplied by 100
    """
    return np.mean(2.0 * np.abs(actual - predicted) / ((np.abs(actual) + np.abs(predicted)) + EPSILON))

def _naive_forecasting(actual: np.ndarray, seasonality: int = 1):
    """ Naive forecasting method which just repeats previous samples """
    return actual[:-seasonality]

def mase(actual: np.ndarray, predicted: np.ndarray, seasonality: int = 1):
    """
    Mean Absolute Scaled Error
    Baseline (benchmark) is computed with naive forecasting (shifted by @seasonality)
    """
    return mae(actual, predicted) / mae(actual[seasonality:], _naive_forecasting(actual, seasonality))

def _error(actual: np.ndarray, predicted: np.ndarray):
    """ Simple error """
    return actual - predicted

def mae(actual: np.ndarray, predicted: np.ndarray):
    """ Mean Absolute Error """
    return np.mean(np.abs(_error(actual, predicted)))

def mse(actual: np.ndarray, predicted: np.ndarray):
    """ Mean Squared Error """
    return np.mean(np.square(_error(actual, predicted)))


def rmse(actual: np.ndarray, predicted: np.ndarray):
    """ Root Mean Squared Error """
    return np.sqrt(mse(actual, predicted))

def compare(l1, l2, l3):
    count = 0
    for i in range(len(l1)):
        if(l1[i] < l2[i]):
            count+=1
        if(l1[i] < l3[i]):
            count+=1
    return count

def publishErr(sensor, error, timestamp):
    #function for bublishing count of ppl called every 15 mins
    client = mqtt.Client("platform")
    client.username_pw_set(username="JWT", password="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA4NzI2ODUsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfMTA3In0.x40nxuvC7xAGbXsuM0HsxE_uzya3oGFfu-pcI09e0e4h_tIuDn6em0NxyAmDjbjECKpQPoCuTlam9BRV8VSZkv3YwYNNATxesQJQ0XYHA9QUTdXzSZgefiLTE00nSq9gPD6O2dddwx6egAr9xcPqlOC6WXOU9mb1pnFoOv0BMymLvsSJndsB4bxMxi3CYrMirFRxFzPaGFZvwRNMmZZ5oGWtLhtLJ7cJ0t0wdD9jV1334AQfCayQvb9n7_6E3ruSZjfUdGMj5jV9lFaChfKjzs2yq15dDfKbbfOkDGwLPrpURH3Y65ycuPyaUn6TTz_EwUF2d5Ai5i2x5cJn_TSQ2vbDJw3BZxcBZ7NxiXoG00OwQ02QXMAryyYJ1SKdr_BPxyy2C2XeZFGg3ip42bUkn92tScFiQtzUp-WaQSFWiZK3D1sdJglPb6l1iEQCI7HfNk0g0ADlvKh_aGTcxy4sjFM69phDfj_UV7T5Of-BV1lk7mifC2tdg1rqqttCMQ9uNbnsrw6YcfsO896zr7uFDaVsG98veUCXQYtuXkbn3XWqxGUM13H6nb2ThlZn2dMxi5EsWKXktyQDtAmB_Sf4akU6MU4uW-d8QpzTv9wPaW2Qo1KagA6KIcdM9AjT9J8BsLd8kzMyuUIpVoioT5REp3kIRACzcLLiY6YxyXCy22U")
    client.connect("131.159.35.132", 1883)

    payload = {
        "username": "group2_2021_ss",
        sensor: error,
        "device_id": 107,
        "timestamp": timestamp
    }
    client.publish("50_107", json.dumps(payload))
    print("Just published " + str(payload) + " to topic 50_107")

def publishPred(count, timestamp):

    client = mqtt.Client("platform")
    client.username_pw_set(username="JWT", password="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA4NzI2ODUsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfMTA3In0.x40nxuvC7xAGbXsuM0HsxE_uzya3oGFfu-pcI09e0e4h_tIuDn6em0NxyAmDjbjECKpQPoCuTlam9BRV8VSZkv3YwYNNATxesQJQ0XYHA9QUTdXzSZgefiLTE00nSq9gPD6O2dddwx6egAr9xcPqlOC6WXOU9mb1pnFoOv0BMymLvsSJndsB4bxMxi3CYrMirFRxFzPaGFZvwRNMmZZ5oGWtLhtLJ7cJ0t0wdD9jV1334AQfCayQvb9n7_6E3ruSZjfUdGMj5jV9lFaChfKjzs2yq15dDfKbbfOkDGwLPrpURH3Y65ycuPyaUn6TTz_EwUF2d5Ai5i2x5cJn_TSQ2vbDJw3BZxcBZ7NxiXoG00OwQ02QXMAryyYJ1SKdr_BPxyy2C2XeZFGg3ip42bUkn92tScFiQtzUp-WaQSFWiZK3D1sdJglPb6l1iEQCI7HfNk0g0ADlvKh_aGTcxy4sjFM69phDfj_UV7T5Of-BV1lk7mifC2tdg1rqqttCMQ9uNbnsrw6YcfsO896zr7uFDaVsG98veUCXQYtuXkbn3XWqxGUM13H6nb2ThlZn2dMxi5EsWKXktyQDtAmB_Sf4akU6MU4uW-d8QpzTv9wPaW2Qo1KagA6KIcdM9AjT9J8BsLd8kzMyuUIpVoioT5REp3kIRACzcLLiY6YxyXCy22U")
    client.connect("131.159.35.132", 1883)

    payload = {
        "username": "group2_2021_ss",
        "bestOnline": count,
        "device_id": 107,
        "timestamp": timestamp
    }
    client.publish("50_107", json.dumps(payload))
    print("Just published " + str(payload) + " to topic 50_107")

    #publishing to sensor node
    client = mqtt.Client("sensor")
    client.connect("test.mosquitto.org")

    client.publish("/topic/nadija/predictions", str(count).zfill(2))
    print("Just published " + str(count).zfill(2) + " to topic /topic/nadija/predictions")

CONSUMER_URL = 'iotplatform.caps.in.tum.de:443'
DEV_JWT = 'eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA5MTQ2OTMsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfODMifQ.mB9292Db6piVAOIcDnOKO5VFO6qWXmLNWuKOKrRN8FIjMnnfyO0XNOr9k2YjtDhHVyo8JI1igbDht8BRsre1zeM6JIsIwCpNZ9polPFb93hHf04xHyn0vsWw5JO2YeY4-ifyX-WifIgpwWfJvbaqZn_nY2P_thmqpH9tNKEOAkZVf3ovV_9WypzYH7z_mCfId0TI9W96L3LJtU6HEorT4A1ft5XXbZxgQZudzENoYg3J2UvFwtI42phMGbor4i9LrHCd9y6uv46s78-qnUgEWpd3qL91wdtJT0JFgkUlc8BwXyPIwJIQWb0-AR090Upv59aIkmIqig4LGO1ogX49eHcasw0Rzp1nT8aBWBY8lJjsZ3bk-rH2EYelr5RARK2q_zKQjDt9mFWTeERyeVmbpkEyur4hwbnse0zGee9hUgair3LCs-2AWYqCkXGqDjFzcAuuLCSCQtXqXw-hENtcnCo8_q_F9G_KzSNqRPZ1VViA_3a-0I7EALzNGsGdrUk_KCaVY2sXmBOX3Px4jVGxxE-IDJPK723DL1dQgHR_IICyNT-wdbaZMSU5GyevMOrCRyKEXnvlSWakXqJagdmw5JVRivcdJ8aNnMMZgllbXYW7wJDTZYwMeyZjqIBkNBfsVuhGGjknsugW4FUZnoqwXS3QuUCgtD7pZt4Kaz_9CZI'

sensorID = 1248
batchSize = 100
searchPath = '/api/consumers/consume/' + str(sensorID) + '/_search?' #base path - search all
countPath = '/api/consumers/consume/' + str(sensorID) + '/_count?' #base path - count all

generated_ts = pd.DataFrame(columns = ['time', 'count'])

ts = time()
curTs = int(ts - (ts % 60))

#we get the counts published in the last 15 minutes if there is more counts, we take last one of them
#we get data from last 15 minutes because in that time interval there must be at least one prediction, 
#since we push preditions on every 15 minutes even if there are no enter/leave commands

weekAgoTs = int(curTs - 15 * 60 * 60)
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

#in the following code, we read prediction from our textfile for each of the models,
#compare it to the real prediction we obtained with the previous code and calculate errors, which we publish

with open('/home/ubuntu/week7task/arima_forecasts.txt') as f:
    line = f.readline()

print(line)
line = line.split()
print(line)

arimaTimestamp = int(line[0])
arimaCount = int(line[1])

print(generated_ts.time[generated_ts.time < datetime.fromtimestamp(arimaTimestamp/1000)])

timestamps = generated_ts.time[generated_ts.time < datetime.fromtimestamp(arimaTimestamp/1000)]

if(len(timestamps) != 0):

    print(type(timestamps[len(timestamps) - 1]))

    print(generated_ts.loc[timestamps[len(timestamps) - 1]])

    ts = timestamps[len(timestamps) - 1]

    realCount = generated_ts.loc[ts]["count"]

    predictedCount = np.array(arimaCount)
    realCount = np.array(realCount)

    print(type(predictedCount))

    print(realCount)
    print(type(realCount))

    maeErr = mae(realCount, predictedCount)
    print("mae" + str(maeErr))

    rmseErr = rmse(realCount, predictedCount)
    print("rmse" + str(rmseErr))

    mapeErr = mape(realCount, predictedCount)
    print("mape" + str(mapeErr))

    smapeErr = smape(realCount, predictedCount)
    print("smape" + str(smapeErr))

    #MASE cannot be computed for just one point since it looks at lag 1

    #if(len(realCount) == 1):
    #    maseErr = mae(realCount, predictedCount)
    #else:
    #    maseErr = mase(realCount, predictedCount)
    #maseErr = mase(np.array([2]), np.array([1]), 0)
    #print("mase" + str(maseErr))

    #push all the metrics to platform
    publishErr("mae_arima", maeErr, arimaTimestamp)
    publishErr("rmse_arima", rmseErr, arimaTimestamp)
    publishErr("mape_arima", mapeErr, arimaTimestamp)
    publishErr("smape_arima", smapeErr, arimaTimestamp)
    #publish("mase", maseErr, timestamp)
    arimaErrors = [maeErr, rmseErr, mapeErr, smapeErr]

with open('/home/ubuntu/week7task/lstm_forecasts.txt') as f:
    line = f.readline()

print(line)
line = line.split()
print(line)

lstmTimestamp = int(line[0])
lstmCount = int(line[1])

print(generated_ts.time[generated_ts.time < datetime.fromtimestamp(lstmTimestamp/1000)])

timestamps = generated_ts.time[generated_ts.time < datetime.fromtimestamp(lstmTimestamp/1000)]

if(len(timestamps) != 0):

    print(type(timestamps[len(timestamps) - 1]))

    print(generated_ts.loc[timestamps[len(timestamps) - 1]])

    ts = timestamps[len(timestamps) - 1]

    realCount = generated_ts.loc[ts]["count"]

    predictedCount = np.array(lstmCount)
    realCount = np.array(realCount)

    print(type(predictedCount))

    print(realCount)
    print(type(realCount))

    maeErr = mae(realCount, predictedCount)
    print("mae" + str(maeErr))

    rmseErr = rmse(realCount, predictedCount)
    print("rmse" + str(rmseErr))

    mapeErr = mape(realCount, predictedCount)
    print("mape" + str(mapeErr))

    smapeErr = smape(realCount, predictedCount)
    print("smape" + str(smapeErr))

    #if(len(realCount) == 1):
    #    maseErr = mae(realCount, predictedCount)
    #else:
    #    maseErr = mase(realCount, predictedCount)
    #maseErr = mase(np.array([2]), np.array([1]), 0)
    #print("mase" + str(maseErr))

    #push all the metrics to platform
    publishErr("mae_lstm", maeErr, lstmTimestamp)
    publishErr("rmse_lstm", rmseErr, lstmTimestamp)
    publishErr("mape_lstm", mapeErr, lstmTimestamp)
    publishErr("smape_lstm", smapeErr, lstmTimestamp)
    #publish("mase", maseErr, timestamp)
    lstmErrors = [maeErr, rmseErr, mapeErr, smapeErr]

with open('/home/ubuntu/week7task/prop_forecasts.txt') as f:
    line = f.readline()

print(line)
line = line.split()
print(line)

propTimestamp = int(line[0])
propCount = int(line[1])

print(generated_ts.time[generated_ts.time < datetime.fromtimestamp(propTimestamp/1000)])

timestamps = generated_ts.time[generated_ts.time < datetime.fromtimestamp(propTimestamp/1000)]

if(len(timestamps) != 0):

    print(type(timestamps[len(timestamps) - 1]))

    print(generated_ts.loc[timestamps[len(timestamps) - 1]])

    ts = timestamps[len(timestamps) - 1]

    realCount = generated_ts.loc[ts]["count"]

    predictedCount = np.array(propCount)
    realCount = np.array(realCount)

    print(type(predictedCount))

    print(realCount)
    print(type(realCount))

    maeErr = mae(realCount, predictedCount)
    print("mae" + str(maeErr))

    rmseErr = rmse(realCount, predictedCount)
    print("rmse" + str(rmseErr))

    mapeErr = mape(realCount, predictedCount)
    print("mape" + str(mapeErr))

    smapeErr = smape(realCount, predictedCount)
    print("smape" + str(smapeErr))

    #if(len(realCount) == 1):
    #    maseErr = mae(realCount, predictedCount)
    #else:
    #    maseErr = mase(realCount, predictedCount)
    #maseErr = mase(np.array([2]), np.array([1]), 0)
    #print("mase" + str(maseErr))

    #push all the metrics to platform
    publishErr("mae_prop", maeErr, propTimestamp)
    publishErr("rmse_prop", rmseErr, propTimestamp)
    publishErr("mape_prop", mapeErr, propTimestamp)
    publishErr("smape_prop", smapeErr, propTimestamp)
    #publish("mase", maseErr, timestamp)
    propErrors = [maeErr, rmseErr, mapeErr, smapeErr]

#we made function for comparing errors which gives us scores for each model
arimaScore = compare(arimaErrors, lstmErrors, propErrors)
lstmScore = compare(lstmErrors, arimaErrors, propErrors)
propScore = compare(propErrors, lstmErrors, arimaErrors)

print(arimaScore)
print(lstmScore)
print(propScore)

#the model with the biggest score wins
if(lstmScore >= arimaScore and lstmScore >= propScore):
    print("lstm wins")
    publishPred(lstmCount, lstmTimestamp)
elif(propScore >= arimaScore and propScore >= lstmScore):
    print("prop wins")
    publishPred(propCount, propTimestamp)
else:
    print("arima wins")
    publishPred(arimaCount, arimaTimestamp)

#femove forecast from txt files to free space
with open('/home/ubuntu/week7task/arima_forecasts.txt', 'r') as fin:
    data = fin.read().splitlines(True)
with open('/home/ubuntu/week7task/arima_forecasts.txt', 'w') as fout:
    fout.writelines(data[1:])

with open('/home/ubuntu/week7task/lstm_forecasts.txt', 'r') as fin:
    data = fin.read().splitlines(True)
with open('/home/ubuntu/week7task/lstm_forecasts.txt', 'w') as fout:
    fout.writelines(data[1:])

with open('/home/ubuntu/week7task/prop_forecasts.txt', 'r') as fin:
    data = fin.read().splitlines(True)
with open('/home/ubuntu/week7task/prop_forecasts.txt', 'w') as fout:
    fout.writelines(data[1:])
