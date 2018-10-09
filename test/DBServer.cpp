//In order to run units tests in clean, controlled environments, it is desirable 
//that each should use a distinct database instance. However, database instances 
//must be assigned port numbers, and these must not collide, so some central 
//authority must coordinate this. This program provides that service by running
//a server on a known port (52000), creating database instances on demand and
//returning the ports on which they are listening. 

#include <cstdlib>
#include <cstdio> //remove
#include <cerrno>
#include <chrono>
#include <map>
#include <random>

#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>

#include <crow.h>
#include <libcuckoo/cuckoohash_map.hh>

#include "Process.h"

bool fetchFromEnvironment(const std::string& name, std::string& target){
	char* val=getenv(name.c_str());
	if(val){
		target=val;
		return true;
	}
	return false;
}

struct ASIOForkCallbacks : public ForkCallbacks{
	boost::asio::io_service& io_service;
	ASIOForkCallbacks(boost::asio::io_service& ios):io_service(ios){}
	void beforeFork() override{
		io_service.notify_fork(boost::asio::io_service::fork_prepare);
	}
	void inChild() override{
		io_service.notify_fork(boost::asio::io_service::fork_child);
	}
	void inParent() override{
		io_service.notify_fork(boost::asio::io_service::fork_parent);
	}
};

std::string dynamoJar="DynamoDBLocal.jar";
std::string dynamoLibs="DynamoDBLocal_lib";

ProcessHandle launchDynamo(unsigned int port, boost::asio::io_service& io_service){
	auto proc=
		startProcessAsync("java",{
			"-Djava.library.path="+dynamoLibs,
			"-jar",
			dynamoJar,
			"-port",
			std::to_string(port),
			"-inMemory"
		},{},ASIOForkCallbacks{io_service},true);
	
	return proc;
}

ProcessHandle launchHelmServer(boost::asio::io_service& io_service){
	auto proc=
		startProcessAsync("helm",{
			"serve"
		},{},ASIOForkCallbacks{io_service},true);
	
	return proc;
}

std::string allocateNamespace(const unsigned int index){
	std::string name="test-"+std::to_string(index);
	
	auto res=runCommandWithInput("kubectl",
R"(apiVersion: nrp-nautilus.io/v1alpha1
kind: Cluster
metadata: 
  name: )"+name,
	  {"create","-f","-"});
	if(res.status){
		std::cout << "Cluster/namespace creation failed: " << res.error << std::endl;
		return "";
	}
	
	//wait for the corresponding namespace to be ready
	while(true){
		res=runCommand("kubectl",{"get","namespace",name,"-o","jsonpath={.status.phase}"});
		if(res.status==0 && res.output=="Active")
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	
	res=runCommand("kubectl",
	  {"get","serviceaccount",name,"-n",name,"-o","jsonpath={.secrets[].name}"});
	if(res.status){
		std::cout << "Finding ServiceAccount failed: " << res.error << std::endl;
		return "";
	}
	std::string credName=res.output;
	
	res=runCommand("kubectl",
	  {"get","secret",credName,"-n",name,"-o","jsonpath={.data.ca\\.crt}"});
	if(res.status){
		std::cout << "Extracting CA data failed: " << res.error << std::endl;
		return "";
	}
	std::string caData=res.output;
	
	res=runCommand("kubectl",{"cluster-info"});
	if(res.status){
		std::cout << "Getting cluster info failed: " << res.error << std::endl;
		return "";
	}
	//sift out the first URL
	auto startPos=res.output.find("http");
	if(startPos==std::string::npos){
		std::cout << "Could not find 'http' in cluster info" << std::endl;
		return "";
	}
	auto endPos=res.output.find((char)0x1B,startPos);
	if(endPos==std::string::npos){
		std::cout << "Could not find '0x1B' in cluster info" << std::endl;
		return "";
	}
	std::string server=res.output.substr(startPos,endPos-startPos);
	
	res=runCommand("kubectl",{"get","secret","-n",name,credName,"-o","jsonpath={.data.token}"});
	if(res.status){
		std::cout << "Extracting token failed: " << res.error << std::endl;
		return "";
	}
	std::string encodedToken=res.output;
	
	res=runCommandWithInput("base64",encodedToken,{"--decode"});
	if(res.status){
		std::cout << "Decoding token failed: " << res.error << std::endl;
		return "";
	}
	std::string token=res.output;
	
	std::ostringstream os;
	os << R"(apiVersion: v1
clusters:
- cluster:
    certificate-authority-data: )"
	  << caData << '\n'
	  << "    server: " << server << '\n'
	  << R"(  name: cluster
contexts:
- context:
    cluster: cluster
    namespace: )" << name << '\n'
	  << "    user: " << name << '\n'
	  << R"(  name: cluster
current-context: cluster
kind: Config
preferences: {}
users:
- name: )" << name << '\n'
	  << R"(  user:
    token: )" << token << '\n';
	
	return os.str();
}

//Launching subprocesses does not work properly once the crow server is running, 
//thanks to asio. The soultion is to instead fork once immediately to create a
//child process which runs this launcher class, and communicate with it via a 
//pipe to request launching further processes when necessary. 
class Launcher{
public:
	Launcher(boost::asio::io_service& io_service,
			 boost::asio::local::datagram_protocol::socket& input_socket,
				   boost::asio::local::datagram_protocol::socket& output_socket)
    : io_service(io_service),
	input_socket(input_socket),
	output_socket(output_socket)
	{}
	
	void operator()(){
		std::vector<char> buffer;
		while(true){
			// Wait for server to write data.
			boost::system::error_code ec
			  =boost::system::errc::make_error_code(boost::system::errc::success);
			do{
				input_socket.receive(boost::asio::null_buffers(), boost::asio::local::datagram_protocol::socket::message_peek, ec);
			}while(ec!=boost::system::errc::success);
			
			// Resize buffer and read all data.
			buffer.resize(input_socket.available());
			std::fill(buffer.begin(), buffer.end(), '\0');
			input_socket.receive(boost::asio::buffer(buffer));
			
			std::string rawString;
			rawString.assign(buffer.begin(), buffer.end());
			std::istringstream ss(rawString);
			
			std::string child;
			ss >> child;
			if(child=="dynamo" || child=="helm"){
				std::string portStr;
				ss >> portStr;
				//std::cout << getpid() << " Got port " << command << std::endl;
				unsigned int port=std::stoul(portStr);
				
				ProcessHandle proc;
				if(child=="dynamo")
					proc=launchDynamo(port,io_service);
				if(child=="helm")
					proc=launchHelmServer(io_service);
				//send the pid of the new process back to our parent
				output_socket.send(boost::asio::buffer(std::to_string(proc.getPid())));
				//give up responsibility for stopping the child process
				proc.detach();
			}
			else if(child=="namespace"){
				unsigned int index;
				ss >> index;
				std::string config=allocateNamespace(index);
				output_socket.send(boost::asio::buffer(std::to_string(config.size())));
				std::size_t sent=0;
				const std::size_t chunk_size=512;
				while(sent<config.size()){
					//output_socket.wait(boost::asio::ip::tcp::socket::wait_write);
					output_socket.send(boost::asio::buffer(config.substr(sent,chunk_size)));
					sent+=chunk_size;
				}
			}
		}
	}
	
private:
	boost::asio::io_service& io_service;
	boost::asio::local::datagram_protocol::socket& input_socket;
	boost::asio::local::datagram_protocol::socket& output_socket;
};

int main(){
	startReaper();
	//figure out where dynamo is
	fetchFromEnvironment("DYNAMODB_JAR",dynamoJar);
	fetchFromEnvironment("DYNAMODB_LIB",dynamoLibs);
	
	struct stat info;
	int err=stat(dynamoJar.c_str(),&info);
	if(err){
		err=errno;
		if(err!=ENOENT)
			std::cerr << "Unable to stat DynamoDBLocal.jar at "+dynamoJar+"; error "+std::to_string(err) << std::endl;
		else
			std::cerr << "Unable to stat DynamoDBLocal.jar; "+dynamoJar+" does not exist" << std::endl;
		return(1);
	}
	err=stat(dynamoLibs.c_str(),&info);
	if(err){
		err=errno;
		if(err!=ENOENT)
			std::cerr << "Unable to stat DynamoDBLocal_lib at "+dynamoLibs+"; error "+std::to_string(err) << std::endl;
		else
			std::cerr << "Unable to stat DynamoDBLocal_lib; "+dynamoLibs+" does not exist" << std::endl;
		return(1);
	}
	
	{ //make sure kubernetes is in the right state for federation
		std::cout << "Installing federation role" << std::endl;
		auto res=runCommand("kubectl",
		  {"apply","-f","https://gitlab.com/ucsd-prp/nrp-controller/raw/master/federation-role.yaml"});
		if(res.status){
			std::cerr << "Unable to deploy federation role: " << res.error << std::endl;
			return(1);
		}
		
		std::cout << "Installing federation controller" << std::endl;
		res=runCommand("kubectl",
		  {"apply","-f","https://gitlab.com/ucsd-prp/nrp-controller/raw/master/deploy.yaml"});
		if(res.status){
			std::cerr << "Unable to deploy federation controller: " << res.error << std::endl;
			return(1);
		}
		std::cout << "Done initializing kubernetes" << std::endl;
	}
	
	{ //demonize
		auto group=setsid();
		
		for(int i = 0; i<FOPEN_MAX; i++)
			close(i);
		//redirect fds 0,1,2 to /dev/null
		open("/dev/null", O_RDWR); //stdin
		dup(0); //stdout
		dup(0); //stderr
	}
	
	//stop background thread temporarily during the delicate asio/fork dance
	stopReaper();
	
	boost::asio::io_service io_service;
	//create a set of connected sockets for inter-process communication
	boost::asio::local::datagram_protocol::socket parent_output_socket(io_service);
	boost::asio::local::datagram_protocol::socket child_input_socket(io_service);
	boost::asio::local::connect_pair(parent_output_socket, child_input_socket);
	
	boost::asio::local::datagram_protocol::socket parent_input_socket(io_service);
	boost::asio::local::datagram_protocol::socket child_output_socket(io_service);
	boost::asio::local::connect_pair(child_output_socket, parent_input_socket);
	
	io_service.notify_fork(boost::asio::io_service::fork_prepare);
	int child=fork();
	if(child<0){
		std::cerr << "fork failed: Error " << child << std::endl;
		return 1;
	}
	if(child==0){
		io_service.notify_fork(boost::asio::io_service::fork_child);
		parent_input_socket.close();
		parent_output_socket.close();
		startReaper();
		Launcher(io_service, child_input_socket, child_output_socket)();
		return 0;
	}
	//else still the parent process
	ProcessHandle launcher(child);
	io_service.notify_fork(boost::asio::io_service::fork_parent);
	child_input_socket.close();
	child_output_socket.close();
	startReaper();
	
	cuckoohash_map<unsigned int, ProcessHandle> soManyDynamos;
	std::mutex helmLock;
	std::mutex launcherLock;
	ProcessHandle helmHandle;
	const unsigned int minPort=52001, maxPort=53000;
	unsigned int namespaceIndex=0;
	
	auto allocatePort=[&]()->unsigned int{
		///insert an empty handle into the table to reserve a port number
		unsigned int port=minPort;
		while(true){ //TODO: maybe at some point stop looping
			bool success=true;
			soManyDynamos.upsert(port,[&](const ProcessHandle&){
				success=false;
			},ProcessHandle{});
			if(success)
				break;
			port++;
			if(port==maxPort)
				port=minPort;
		}
		return port;
	};
	
	auto runDynamo=[&](unsigned int port)->ProcessHandle{
		std::ostringstream ss;
		ss << "dynamo" << ' ' << port;
		std::lock_guard<std::mutex> lock(launcherLock);
		parent_output_socket.send(boost::asio::buffer(ss.str()));
		//should be easily large enough to hold the string representation of any pid
		std::vector<char> buffer(128,'\0');
		parent_input_socket.receive(boost::asio::buffer(buffer));
		return ProcessHandle(std::stoul(buffer.data()));
	};
	
	auto startHelm=[&](){
		std::cout << "Got request to start helm" << std::endl;
		std::lock_guard<std::mutex> lock(helmLock);
		//at this point we have ownership to either create or use the process 
		//handle for helm
		if(helmHandle)
			return crow::response(200); //already good, release lock and exit
		//otherwise, create child process
		std::ostringstream ss;
		ss << "helm" << ' ' << 8879;
		std::lock_guard<std::mutex> lock2(launcherLock);
		parent_output_socket.send(boost::asio::buffer(ss.str()));
		//should be easily large enough to hold the string representation of any pid
		std::vector<char> buffer(128,'\0');
		parent_input_socket.receive(boost::asio::buffer(buffer));
		helmHandle=ProcessHandle(std::stoul(buffer.data()));
		return crow::response(200);
	};
	
	auto stopHelm=[&](){
		std::cout << "Got request to stop helm" << std::endl;
		//need ownership
		std::lock_guard<std::mutex> lock(helmLock);
		helmHandle=ProcessHandle(); //destroy by replacing with empty handle
		return 200;
	};
	
	auto allocateNamespace=[&](){
		std::cout << "Got request for a namespace" << std::endl;
		std::ostringstream ss;
		std::lock_guard<std::mutex> lock(launcherLock);
		ss << "namespace" << ' ' << namespaceIndex << '\0';
		namespaceIndex++;
		parent_output_socket.send(boost::asio::buffer(ss.str()));
		
		std::string config;
		std::vector<char> buffer(128,'\0');
		parent_input_socket.receive(boost::asio::buffer(buffer));
		unsigned long msgSize=std::stoul(buffer.data());
		//std::cout << "message size: " << msgSize << std::endl;
		while(config.size()<msgSize){
			//parent_input_socket.wait(boost::asio::ip::tcp::socket::wait_read);
			//auto available=parent_input_socket.available();
			//std::cout << available << " bytes available" << std::endl;
			std::vector<char> buffer(4096,'\0');
			parent_input_socket.receive(boost::asio::buffer(buffer));
			config+=std::string(buffer.data());
		}
		return crow::response(200,config);
	};
	
	auto getPort=[&]{
		auto port=allocatePort();
		return std::to_string(port);
	};
	
	auto freePort=[&](unsigned int port){
		soManyDynamos.erase(port);
		return crow::response(200);
	};
	
	auto create=[&]{
		std::cout << "Got request to start dynamo" << std::endl;
		if(launcher.done())
			return crow::response(500,"Child launcher process has ended");
		auto port=allocatePort();
		//at this point we own this port; start the instance
		ProcessHandle dyn=runDynamo(port);
		if(!dyn){
			freePort(port);
			return crow::response(500,"Unable to start Dynamo");
		}
		std::cout << "Started child process " << dyn.getPid() << std::endl;
		//insert the handle into the table, replacing we dummy put there earlier
		soManyDynamos.upsert(port,[&](ProcessHandle& dummy){
			dummy=std::move(dyn);
		},ProcessHandle{});
		return crow::response(200,std::to_string(port));
	};
	
	auto remove=[&](unsigned int port){
		std::cout << "Got request to stop dynamo on port " << port << std::endl;
		soManyDynamos.erase(port);
		std::cout << "Erased process handle for port " << port << std::endl;
		return crow::response(200);
	};
	
	auto stop=[](){
		std::cout << "Got request to stop dynamo server" << std::endl;
		kill(getpid(),SIGTERM);
		return crow::response(200);
	};
	
	crow::SimpleApp server;
	
	CROW_ROUTE(server, "/port/allocate").methods("GET"_method)(getPort);
	CROW_ROUTE(server, "/port/<int>").methods("DELETE"_method)(freePort);
	CROW_ROUTE(server, "/dynamo/create").methods("GET"_method)(create);
	CROW_ROUTE(server, "/dynamo/<int>").methods("DELETE"_method)(remove);
	CROW_ROUTE(server, "/helm").methods("GET"_method)(startHelm);
	CROW_ROUTE(server, "/helm").methods("DELETE"_method)(stopHelm);
	CROW_ROUTE(server, "/namespace").methods("GET"_method)(allocateNamespace);
	CROW_ROUTE(server, "/stop").methods("PUT"_method)(stop);

	std::cout << "Starting http server" << std::endl;
	{
		std::ofstream touch(".test_server_ready");
	}
	server.loglevel(crow::LogLevel::Warning);
	server.port(52000).run();
	{
		::remove(".test_server_ready");
	}
}