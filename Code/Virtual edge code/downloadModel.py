from minio import Minio
import pandas as pd
import pickle
import io
import os
from time import time
import paho.mqtt.client as mqtt
import json

minioClient = Minio('138.246.235.1:9000',
                            access_key='nadijaborovina123',
                            secret_key='nekipassglupi123',
                            secure=False)

data_obj_arima = minioClient.get_object("test-model", "arima_model_semifinal.pk")
data_obj_lstm = minioClient.get_object("test-model", "lstm_model_semifinal_local.pk")
data_obj_prop = minioClient.get_object("test-model", "prophet_model_semifinal_local.pk")

with open('arima_model', 'wb') as file_data:
        for d in data_obj_arima.stream():
            file_data.write(d)

with open('lstm_model', 'wb') as file_data:
        for d in data_obj_lstm.stream():
            file_data.write(d)

with open('prop_model', 'wb') as file_data:
        for d in data_obj_prop.stream():
            file_data.write(d)
print("Model successfully downloaded.")
