import httplib2
from oauth2client import client
from oauth2client import file
from oauth2client.client import flow_from_clientsecrets
from oauth2client.file import Storage

from datetime import datetime
from datetime import timedelta

from apiclient.discovery import build
from apiclient.http import MediaIoBaseUpload

import io
import csv

from googleapiclient.errors import HttpError

import sys
import json


class bjfGoogle:

	def __init__(self, api_key=None):
		self.api_key=api_key

	def Authenticate(self,client_secret_file, credential_store, scopeRequired):
		# https://developers.google.com/api-client-library/python/guide/aaa_oauth
		authStorage=Storage(credential_store)
		self.cachedAuthorisation=authStorage.get()
		if self.cachedAuthorisation is None or self.cachedAuthorisation.invalid or not self.cachedAuthorisation.has_scopes(scopeRequired):
			flow = client.flow_from_clientsecrets(client_secret_file,
				scopeRequired,
				redirect_uri='urn:ietf:wg:oauth:2.0:oob')
			auth_uri = flow.step1_get_authorize_url()
			print ("Please visit the following URL and authenticate:")
			print 
			print (self.ShortenUrl(auth_uri))
			print
			auth_code = input('Enter the auth code: ')
			self.cachedAuthorisation = flow.step2_exchange(auth_code)

		#if (self.cachedAuthorisation.token_expiry - datetime.utcnow()) < timedelta(minutes=5):
		#	http = self.cachedAuthorisation.authorize(httplib2.Http())
		#	self.cachedAuthorisation.refresh(http)

		# create an authorised http
		http=httplib2.Http()
		self.HTTPauthed=self.cachedAuthorisation.authorize(http)

		authStorage.put(self.cachedAuthorisation)

		return 

	def AuthorisedHTTP(self):
		#http=httplib2.Http()
		#http = self.cachedAuthorisation.authorize(http)

		# if we haven't expired, return the http, otherwise, refresh it
		if (self.cachedAuthorisation.token_expiry - datetime.utcnow()) < timedelta(minutes=5):
			self.HTTPauthed = self.cachedAuthorisation.authorize(httplib2.Http())
			self.cachedAuthorisation.refresh(self.HTTPauthed)

		return self.HTTPauthed


#	scope='https://www.googleapis.com/auth/photos https://www.googleapis.com/auth/userinfo.profile',
	def GetPhotoService(self,access_token):
		user_agent='bjfapp'
		gd_client = gdata.photos.service.PhotosService(source=user_agent,
			email="default",
			additional_headers={'Authorization' : 'Bearer %s' % access_token})
		return gd_client

# sheets - RO - "https://www.googleapis.com/auth/spreadsheets.readonly"
# sheets - RW - https://www.googleapis.com/auth/spreadsheets
	def GetSheetsService(self):
		discoveryUrl = ('https://sheets.googleapis.com/$discovery/rest?'
		                    'version=v4')
		service=build('sheets', 'v4', http=self.AuthorisedHTTP(),discoveryServiceUrl=discoveryUrl)
		return service

# ='https://www.googleapis.com/auth/fusiontables'
# https://developers.google.com/api-client-library/python/guide/aaa_oauth
	def GetFusionService(self):
		service=build("fusiontables","v2",http=self.AuthorisedHTTP())
		return service

	def ShortenUrl(self, longUrl):
		# quick check
		if self.api_key is None:
			return longUrl
		try:
			service=build('urlshortener', 'v1', developerKey=self.api_key)
			url=service.url()
			body={'longUrl':longUrl}
			resp=url.insert(body=body).execute()
			return resp['id']
		except HttpError:
			return longUrl
		except:
			print ("Exception occurred ",sys.exc_info()[0])
			return longUrl

class bjfSheetsService:

	def __init__(self, bjfGoogleInstance):
		self.bjfGinstance=bjfGoogleInstance
		self.service=build('sheets', 'v4',http=self.bjfGinstance.AuthorisedHTTP())

	def AppendSheetRange(self,sheetID, rowValues, rangeName, valInputOption="USER_ENTERED", insertOption="INSERT_ROWS"):
		body={ 'values':rowValues }
		self.service.spreadsheets().values().append(spreadsheetId=sheetID, range=rangeName, body=body, valueInputOption=valInputOption,insertDataOption=insertOption).execute()

	def UpdateSheetRange(self, sheetID, rowValues, rangeName, valInputOption="USER_ENTERED"):
		body={ 'values':rowValues }
		self.service.spreadsheets().values().update(spreadsheetId=sheetID, range=rangeName, body=body, valueInputOption=valInputOption).execute()


class bjfFusionService:

	def __init__(self, bjfGoogleInstance):
		self.bjfGinstance=bjfGoogleInstance
		self.service=build("fusiontables","v2",http=self.bjfGinstance.AuthorisedHTTP())

	def GetTableByName(self,name):
		# list all tables 
		found=self.service.table().list().execute()
		while "items" in found:
			for each in found["items"]:
				if each["name"]==name:
					return self.OpenTable(each["tableId"])
			if not "nextPageToken" in found:
				break
			found=self.service.table().list(pageToken=found["nextPageToken"]).execute()
		return None

	def CreateTable(self,body):
		return self.service.table().insert(body=body).execute();

	def OpenTable(self,tableid):
		return self.service.table().get(tableId=tableid).execute();

	def InsertRowData(self,tableObject, rowDictionary):

		sump=io.StringIO()
		w=csv.writer(sump, quoting=csv.QUOTE_ALL)
		w.writerows(rowDictionary)
		mu=MediaIoBaseUpload(sump,mimetype="application/octet-stream")
		return self.service.table().importRows(tableId=tableObject["tableId"], media_body=mu).execute()








