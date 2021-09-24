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
from time import sleep
from sklearn.preprocessing import MinMaxScaler

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
    #print(mae(actual, predicted))
    #print(actual[seasonality:])
    #print(_naive_forecasting(actual, seasonality))
    if (mae(actual[seasonality:], _naive_forecasting(actual, seasonality))==0):
        return mae(actual, predicted)
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

def compare(l1, l2, l3):
    count = 0
    for i in range(len(l1)):
        if(l1[i] < l2[i]):
            count+=1
        if(l1[i] < l3[i]):
            count+=1
    return count

def predict(counts, timestamps):
    client = mqtt.Client("platform")
    client.username_pw_set(username="JWT", password="eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjA4NzI2ODUsImlzcyI6ImlvdHBsYXRmb3JtIiwic3ViIjoiNTBfMTA3In0.x40nxuvC7xAGbXsuM0HsxE_uzya3oGFfu-pcI09e0e4h_tIuDn6em0NxyAmDjbjECKpQPoCuTlam9BRV8VSZkv3YwYNNATxesQJQ0XYHA9QUTdXzSZgefiLTE00nSq9gPD6O2dddwx6egAr9xcPqlOC6WXOU9mb1pnFoOv0BMymLvsSJndsB4bxMxi3CYrMirFRxFzPaGFZvwRNMmZZ5oGWtLhtLJ7cJ0t0wdD9jV1334AQfCayQvb9n7_6E3ruSZjfUdGMj5jV9lFaChfKjzs2yq15dDfKbbfOkDGwLPrpURH3Y65ycuPyaUn6TTz_EwUF2d5Ai5i2x5cJn_TSQ2vbDJw3BZxcBZ7NxiXoG00OwQ02QXMAryyYJ1SKdr_BPxyy2C2XeZFGg3ip42bUkn92tScFiQtzUp-WaQSFWiZK3D1sdJglPb6l1iEQCI7HfNk0g0ADlvKh_aGTcxy4sjFM69phDfj_UV7T5Of-BV1lk7mifC2tdg1rqqttCMQ9uNbnsrw6YcfsO896zr7uFDaVsG98veUCXQYtuXkbn3XWqxGUM13H6nb2ThlZn2dMxi5EsWKXktyQDtAmB_Sf4akU6MU4uW-d8QpzTv9wPaW2Qo1KagA6KIcdM9AjT9J8BsLd8kzMyuUIpVoioT5REp3kIRACzcLLiY6YxyXCy22U")
    client.connect("131.159.35.132", 1883)

    print(str(len(counts)))
    print(counts)
    print(str(len(timestamps)))
    print(timestamps)

    for i in range(len(counts)):
        ct = counts[i]
        if (ct < 0):
            ct = 0
        ts = int(timestamps[i].to_pydatetime().timestamp() * 1000)
        payload = {
            "username": "group2_2021_ss",
            "bestOffline": ct,
            "device_id": 107,
            "timestamp": ts
        }
        client.publish("50_107", json.dumps(payload))
        print("Just published " + str(payload) + " to topic 50_107")
        sleep(2)

#getting the data for the last day

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

timestamps = resampled_15min_df.index[-4*24:]

#read model
f = open("/home/ubuntu/week7task/arima_model", 'rb')
model = pd.read_pickle(f)

#making predictions for last day
#last day is 4*24 obs
arimaForecast = model.predict(resampled_15min_df['time'][- 4 * 24], resampled_15min_df['time'][len(resampled_15min_df)-1])

print(arimaForecast)
print(type(arimaForecast))

realCount = resampled_15min_df['count'][- 4 * 24 : ]
predictedCount = arimaForecast.values
arimaPredicted = predictedCount

print(type(arimaForecast.index))
print(type(realCount))
print(type(predictedCount))

#calculating errors
arimaMaeErr = mae(realCount, predictedCount)
print("mae" + str(arimaMaeErr))

arimaRmseErr = rmse(realCount, predictedCount)
print("rmse" + str(arimaRmseErr))

arimaMapeErr = mape(realCount, predictedCount)
print("mape" + str(arimaMapeErr))

arimaSmapeErr = smape(realCount, predictedCount)
print("smape" + str(arimaSmapeErr))

arimaMaseErr = mase(realCount, predictedCount)
print("mase" + str(arimaMaseErr))

arimaErrors = [arimaMaeErr, arimaRmseErr, arimaMapeErr, arimaSmapeErr, arimaMaseErr]

#read model
f = open("/home/ubuntu/week7task/lstm_model", 'rb')
lstm_model = pd.read_pickle(f)

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

#make prediction for the last day
lstmPredict = lstm_model().predict(datasetX)

lstmPredict = scaler.inverse_transform(lstmPredict)

timestamp = int(resampled_15min_df.index[- 4 * 24 - 1].to_pydatetime().timestamp() * 1000)
print(timestamp)

lstmForecast = pd.DataFrame(columns = ['time', 'count'])

for i in range(4*24):
    count = int(lstmPredict[i])
    timestamp = timestamp + 900000
    print(timestamp)
    print(count)
    df_row = pd.DataFrame([[timestamp, count]], columns=['time', 'count'])
    lstmForecast = lstmForecast.append(df_row)

lstmForecast.index = lstmForecast.time
lstmForecast.index.name = 'index'
print(lstmForecast)

predictedCount = lstmForecast['count'].values[:]
print(predictedCount)
lstmPredicted = predictedCount

#calculating errors
lstmMaeErr = mae(realCount, predictedCount)
print("mae" + str(lstmMaeErr))

lstmRmseErr = rmse(realCount, predictedCount)
print("rmse" + str(lstmRmseErr))

lstmMapeErr = mape(realCount, predictedCount)
print("mape" + str(lstmMapeErr))

lstmSmapeErr = smape(realCount, predictedCount)
print("smape" + str(lstmSmapeErr))

lstmMaseErr = mase(realCount, predictedCount)
print("mase" + str(lstmMaseErr))

lstmErrors = [lstmMaeErr, lstmRmseErr, lstmMapeErr, lstmSmapeErr, lstmMaseErr]

#read prophet model
f = open("/home/ubuntu/week7task/prop_model", 'rb')
prop_model = pd.read_pickle(f)

with open('/home/ubuntu/week7task/countDays.txt') as f:
        countFile = f.readlines()

print(countFile[0])
countFile = int(countFile[0])

future = prop_model.make_future_dataframe(freq='15min', periods=countFile * 4 * 24)
future["floor"] = 0
future["cap"] = 45
future = future[~(future["ds"].isna())]
print(future)

#predicting the data for the last day
forcastsPropeth = prop_model.predict(future)
print(forcastsPropeth)

predictedCount = forcastsPropeth['yhat'].values[- 4 * 24:]
propPredicted = predictedCount

#calculating errors
propMaeErr = mae(realCount, predictedCount)
print("mae" + str(propMaeErr))

propRmseErr = rmse(realCount, predictedCount)
print("rmse" + str(propRmseErr))

propMapeErr = mape(realCount, predictedCount)
print("mape" + str(propMapeErr))

propSmapeErr = smape(realCount, predictedCount)
print("smape" + str(propSmapeErr))

propMaseErr = mase(realCount, predictedCount)
print("mase" + str(propMaseErr))

propErrors = [propMaeErr, propRmseErr, propMapeErr, propSmapeErr, propMaseErr]

with open('/home/ubuntu/week7task/countDays.txt', 'w') as f:
    f.write(str(countFile + 1))

#picking best model

arimaScore = compare(arimaErrors, lstmErrors, propErrors)
lstmScore = compare(lstmErrors, arimaErrors, propErrors)
propScore = compare(propErrors, lstmErrors, arimaErrors)

print(arimaScore)
print(lstmScore)
print(propScore)

print(arimaForecast)
print(lstmForecast)
print(forcastsPropeth)

#the one with the biggest score wins, and its predictions are being pushed
if(lstmScore >= arimaScore and lstmScore >= propScore):
    print("lstm wins")
    predict(lstmPredicted, timestamps)
elif(propScore >= arimaScore and propScore >= lstmScore):
    print("prop wins")
    predict(propPredicted, timestamps)
else:
    print("arima wins")
    predict(arimaPredicted, timestamps)
