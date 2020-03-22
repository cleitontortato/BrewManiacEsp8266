#include <Arduino.h>
#include <pgmspace.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include "config.h"

static  char* _username;
static  char* _password;

#if SerialDebug == true
#define DBG_PRINT(...) DebugPort.print(__VA_ARGS__)
#define DBG_PRINTLN(...) DebugPort.println(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#define DBG_PRINTLN(...)
#endif

static ESP8266WebServer server(UPDATE_SERVER_PORT);
static ESP8266HTTPUpdateServer httpUpdater;
//holds the current upload
static File fsUploadFile;

extern FS& FileSystem;
#include "data_edit_html_gz.h"

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

static bool handleFileRead(String path){
  DBG_PRINTLN("handleFileRead: " + path);
  if(_username != NULL && _password != NULL && !server.authenticate(_username, _password)){

 		server.requestAuthentication();
 		return false;
	}

  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(FileSystem.exists(pathWithGz) || FileSystem.exists(path)){
    if(FileSystem.exists(pathWithGz))
      path += ".gz";
    File file = FileSystem.open(path, "r");
    /*size_t sent = */server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

static void handleFileUpload(void){
 	if(_username != NULL && _password != NULL && !server.authenticate(_username, _password))
 		return server.requestAuthentication();

  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    DBG_PRINT("handleFileUpload Name: "); DBG_PRINTLN(filename);
    fsUploadFile = FileSystem.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //DBG_PRINT("handleFileUpload Data: "); DBG_PRINTLN(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    DBG_PRINT("handleFileUpload Size: "); DBG_PRINTLN(upload.totalSize);
  }
}

static void handleFileDelete(void){
 	if(_username != NULL && _password != NULL && !server.authenticate(_username, _password))
 		return server.requestAuthentication();

  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DBG_PRINTLN("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!FileSystem.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  FileSystem.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

static void handleFileCreate(void){
	if(_username != NULL && _password != NULL && !server.authenticate(_username, _password))
 		return server.requestAuthentication();

  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  DBG_PRINTLN("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(FileSystem.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = FileSystem.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

static void handleFileList(void) {
 	if(_username != NULL && _password != NULL && !server.authenticate(_username, _password))
 		return server.requestAuthentication();

  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String path = server.arg("dir");
  DBG_PRINTLN("handleFileList: " + path);
  Dir dir = FileSystem.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}

void ESPUpdateServer_setup(const char* user, const char* pass){

  //SERVER INIT
  //list directory
  _username=(char*)user;
  _password=(char*)pass;
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on(FILE_MANAGEMENT_PATH, HTTP_GET, [](){
 	if(_username != NULL && _password != NULL && !server.authenticate(_username, _password))
 		return server.requestAuthentication();

	server.sendHeader("Content-Encoding", "gzip");
	server.send_P(200,"text/html",edit_htm_gz,edit_htm_gz_len);
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  //called when the url is not defined here
  //use it to load content from file system
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });


 // Flash update server
	httpUpdater.setup(&server,SYSTEM_UPDATE_PATH,user,pass);

  server.begin();
  DBG_PRINTLN("HTTP Update server started\n");

}


void ESPUpdateServer_loop(void){
  server.handleClient();
}
