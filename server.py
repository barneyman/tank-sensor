from flask import Flask, request, send_from_directory
from concurrent.futures import ThreadPoolExecutor
import json

from bjfGoogle import bjfGoogle, bjfFusionService, bjfSheetsService
#import bjfGoogle

from datetime import datetime
from datetime import timedelta

app=Flask(__name__)

@app.route("/")
def main():
    return "Welcome!"

upgradesDict={ 'tank_1.0':'TankSensor_1_1.bin','tank_1.1':'TankSensor_1_2.bin','tank_1.2':'TankSensor_1_3.bin' }
FlaskPort=5000

@app.route('/upgrades/<path:filename>')
def send_js(filename):
	print("sending "+filename)
	return send_from_directory('c:\\scribble\\upgrades', filename)

@app.route("/data", methods=['GET', 'POST'])
def data():
	if request.method == 'GET':
		print( "data GET" )
		return "data"
	else:
		if not request.json:
			print ("error")
			print (request.data)
			return "error"

		executor.submit(ProcessJSON, request.json)

		try:
			# create a json blob to send back
			timenow=datetime.now()
			returnBlob = {'localTime': timenow.strftime("%H:%M:%S"), 'reset': False}
			# get the version running
			clientVer=request.json['version']
			if clientVer in upgradesDict:
				#upgradeDict={'host':{{ request.host.split(':')[0] }}, 'port':FlaskPort, 'url':upgradesDict[clientVer]}
				#upgradeDict={'host':'192.168.43.22', 'port':FlaskPort, 'url':'upgrades/'+upgradesDict[clientVer]}
				upgradeDict={'url':'upgrades/'+upgradesDict[clientVer]}
				returnBlob['upgrade']=upgradeDict
				print("upgrade potential!")

			return json.dumps(returnBlob, separators=(',', ':'));

		except Exception as e:
			print ("Exception occurred ",e)
			return "bad";

def ProcessJSON(jsonData):
	#print (jsonData)
	#print("=======")
	#print (jsonData["data"])
	# work out how many times we have to do this
	try:
		rowValues=[]
	
		utcNow=datetime.utcnow()
		# 'current' iteration is 'now' so subtract
		currentIter=jsonData["current"]
		intervalSeconds=jsonData["intervalS"]
		utcStart=utcNow-timedelta(seconds=jsonData["intervalS"]*(len(jsonData["data"]))-1)
		for reading in jsonData["data"]:
			#print (reading)
			utcForIteration=utcNow-(timedelta(seconds=intervalSeconds*(currentIter-reading["iter"])))
			lastRowValues=[utcForIteration.strftime("%Y-%m-%d %H:%M:%S"), reading["iter"], reading["tempC"],reading["pressMB"],reading["humid%"], reading["distCM"] ]
			# DEBUG - seive to minutes only
			#if reading["iter"] % 6 == 0:
			rowValues.append(lastRowValues)


		# add to history
		# DEBUG
		if len(rowValues)>0 :
			sheetsService.AppendSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues, 'HISTORY_DATA')
			fusion.InsertRowData(tpTable,rowValues)

		# replace 'lastRead'
		rowValues=[lastRowValues]
		lastRowValues.append(utcNow.strftime("%Y-%m-%d %H:%M:%S"))
		sheetsService.UpdateSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues,'LATEST_DATA')
		print( "processed")
	except Exception as e:
		print ("Async Exception occurred ",e)


if __name__ == "__main__":
	thisG=bjfGoogle()
	thisG.Authenticate("c:\scribble\client_secret.json","c:\scribble\credentialStore.credentials", "https://www.googleapis.com/auth/spreadsheets https://www.googleapis.com/auth/fusiontables")
	# create a fusion table
	fusion=bjfFusionService(thisG)
	sheetsService=bjfSheetsService(thisG)
	# seeif our table is there
	tableName="tempPressure"
	tpTable=fusion.GetTableByName(tableName)

	if tpTable==None:
		print("Creating fusion Table ...")
		tableDef={"name":tableName,"isExportable":False, "columns":[{"name":"whenUTC","type":"DATETIME"},{"name":"iter","type":"NUMBER"},{"name":"tempC","type":"NUMBER"},{"name":"pressureMB","type":"NUMBER"},{"name":"humidity%","type":"NUMBER"},{"name":"distanceCM","type":"NUMBER"}]  }
		tpTable=fusion.CreateTable(tableDef)

	if tpTable!=None:
		print("running")
		executor = ThreadPoolExecutor(2)
		app.run(host="0.0.0.0", port=FlaskPort)









