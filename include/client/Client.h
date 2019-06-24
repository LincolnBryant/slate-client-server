#ifndef SLATE_CLIENT_H
#define SLATE_CLIENT_H

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <functional>

#include "rapidjson/document.h"
#include "rapidjson/pointer.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "HTTPRequests.h"

#if ! ( __APPLE__ && __MACH__ )
	//Whether to use CURLOPT_CAINFO to specifiy a CA bundle path.
	//According to https://curl.haxx.se/libcurl/c/CURLOPT_CAINFO.html
	//this should not be used on Mac OS
	#define USE_CURLOPT_CAINFO
#endif

struct upgradeOptions{
	bool assumeYes;
};

struct GroupListOptions{
	bool user;
	
	GroupListOptions():user(false){}
};

struct GroupInfoOptions{
	std::string groupName;
};

struct GroupCreateOptions{
	std::string groupName;
	std::string scienceField;
};

struct GroupUpdateOptions{
	std::string groupName;
	std::string email;
	std::string phone;
	std::string scienceField;
	std::string description;
};

struct GroupDeleteOptions{
	std::string groupName;
	bool assumeYes;
	
	GroupDeleteOptions():assumeYes(false){}
};

struct ClusterListOptions{
	std::string group;
};

struct ClusterInfoOptions{
	std::string clusterName;
};

struct ClusterCreateOptions{
	std::string clusterName;
	std::string groupName;
	std::string orgName;
	std::string kubeconfig;
	bool assumeYes;
	
	ClusterCreateOptions():assumeYes(false){}
};

///A physical location on the Earth
struct GeoLocation{
	double lat, lon;
};

std::ostream& operator<<(std::ostream& os, const GeoLocation& gl);
std::istream& operator>>(std::istream& is, GeoLocation& gl);

struct ClusterOptions{
	std::string clusterName;
};

struct ClusterUpdateOptions : public ClusterOptions{
	std::string orgName;
	bool reconfigure;
	std::string kubeconfig;
	std::vector<GeoLocation> locations;
	bool assumeYes;
	
	ClusterUpdateOptions():reconfigure(false),assumeYes(false){}
};

struct ClusterDeleteOptions : public ClusterOptions{
	bool assumeYes;
	bool force;
	
	ClusterDeleteOptions():assumeYes(false),force(false){}
};

struct GroupClusterAccessOptions : public ClusterOptions{
	std::string groupName;
};

struct ClusterAccessListOptions : public ClusterOptions{
};

struct GroupClusterAppUseListOptions : public ClusterOptions{
	std::string groupName;
};

struct GroupClusterAppUseOptions : public ClusterOptions{
	std::string groupName;
	std::string appName;
};

struct ClusterPingOptions : public ClusterOptions{
};

struct ApplicationOptions{
	bool devRepo;
	bool testRepo;
	
	ApplicationOptions():devRepo(false),testRepo(false){}
};

struct ApplicationConfOptions : public ApplicationOptions{
	std::string appName;
	std::string outputFile;
};

struct ApplicationInstallOptions : public ApplicationOptions{
	ApplicationInstallOptions():fromLocalChart(false){}

	std::string appName;
	std::string cluster;
	std::string group;
	std::string configPath;
	bool fromLocalChart;
};

struct InstanceListOptions{
	std::string group;
	std::string cluster;
};

struct InstanceOptions{
	std::string instanceID;
};

struct InstanceDeleteOptions : public InstanceOptions{
	bool force;
	bool assumeYes;
	
	InstanceDeleteOptions():force(false),assumeYes(false){}
};

struct InstanceLogOptions : public InstanceOptions{
	unsigned long maxLines;
	std::string container;
	bool previousLogs;
	
	InstanceLogOptions():maxLines(20),previousLogs(false){}
};

struct InstanceScaleOptions : public InstanceOptions{
	unsigned long instanceReplicas;
};

struct SecretListOptions{
	std::string group;
	std::string cluster;
};

struct SecretOptions{
	std::string secretID;
};

struct SecretCreateOptions{
	std::string name;
	std::string group;
	std::string cluster;
	std::vector<std::string> data;
};

struct SecretCopyOptions{
	std::string name;
	std::string group;
	std::string cluster;
	std::string sourceID;
};

struct SecretDeleteOptions : public SecretOptions{
	bool force;
	bool assumeYes;
	
	SecretDeleteOptions():force(false),assumeYes(false){}
};

///Try to get the value of an enviroment variable and store it to a string object.
///If the variable was not set \p target will not be modified. 
///\param name the name of the environment variable to get
///\param target the variable into which the environment variable should be 
///              copied, if set
///\return whether the environment variable was set
bool fetchFromEnvironment(const std::string& name, std::string& target);

namespace CLI{
	class App; //fwd decl
}

class Client{
public:
	///\param useANSICodes if true and stdout is a TTY, use ANSI formatting
	///                    for underlines, bold, colors, etc.
	///\param outputWidth maximum number of columns to use for output. If zero, 
	///                   choose automatically, using the terminal width if 
	///                   stdout is a TTY or unlimited if it is not. 
	explicit Client(bool useANSICodes=true, std::size_t outputWidth=0);
	
	void setOutputWidth(std::size_t width);
	
	void setUseANSICodes(bool use);
	
	void printVersion();
	
	void upgrade(const upgradeOptions&);
	
	void createGroup(const GroupCreateOptions& opt);
	
	void updateGroup(const GroupUpdateOptions& opt);
	
	void deleteGroup(const GroupDeleteOptions& opt);
	
	void getGroupInfo(const GroupInfoOptions& opt);
	
	void listGroups(const GroupListOptions& opt);
	
	void createCluster(const ClusterCreateOptions& opt);
	
	void updateCluster(const ClusterUpdateOptions& opt);
	
	void deleteCluster(const ClusterDeleteOptions& opt);
	
	void listClusters(const ClusterListOptions& opt);
	
	void getClusterInfo(const ClusterInfoOptions& opt);
	
	void grantGroupClusterAccess(const GroupClusterAccessOptions& opt);
	
	void revokeGroupClusterAccess(const GroupClusterAccessOptions& opt);
	
	void listGroupWithAccessToCluster(const ClusterAccessListOptions& opt);
	
	void listAllowedApplications(const GroupClusterAppUseListOptions& opt);
	
	void allowGroupUseOfApplication(const GroupClusterAppUseOptions& opt);
	
	void denyGroupUseOfApplication(const GroupClusterAppUseOptions& opt);
	
	void pingCluster(const ClusterPingOptions& opt);
	
	void listApplications(const ApplicationOptions& opt);
	
	void getApplicationConf(const ApplicationConfOptions& opt);
	
	void getApplicationDocs(const ApplicationConfOptions& opt);
	
	void installApplication(const ApplicationInstallOptions& opt);
	
	void listInstances(const InstanceListOptions& opt);
	
	void getInstanceInfo(const InstanceOptions& opt);
	
	void restartInstance(const InstanceOptions& opt);
	
	void deleteInstance(const InstanceDeleteOptions& opt);
	
	void fetchInstanceLogs(const InstanceLogOptions& opt);

	void scaleInstance(const InstanceScaleOptions& opt);

	void listSecrets(const SecretListOptions& opt);

	void getSecretInfo(const SecretOptions& opt);
	
	void createSecret(const SecretCreateOptions& opt);
	
	void copySecret(const SecretCopyOptions& opt);

	void deleteSecret(const SecretDeleteOptions& opt);

	bool clientShouldPrintOnlyJson() const;

	std::string orderBy = "";
	
private:
	///\param configPath the filesystem path to the user's selected kubeconfig. If
	///                  empty, attempt autodetection. 
	///\param assumeYes assume yes/default for questions which would be asked 
	///                 interactively of the user
	///\return the data of a kubeconfig which allows access to an NRP cluster on the 
	///        kubernetes cluster
	std::string extractClusterConfig(std::string configPath, bool assumeYes);

	///Get the default path to the user's API endpoint file
	std::string getDefaultEndpointFilePath();
	///Get the default path to the user's credential file
	std::string getDefaultCredFilePath();
	
	std::string fetchStoredCredentials();
	
	std::string getToken();
	
	std::string getEndpoint();
	
	std::string makeURL(const std::string& path){
		return(getEndpoint()+"/"+apiVersion+"/"+path+"?token="+getToken());
	}
	
	httpRequests::Options defaultOptions();
	
#ifdef USE_CURLOPT_CAINFO
	void detectCABundlePath();
#endif
	
	std::string underline(std::string s) const;
	std::string bold(std::string s) const;
	
	struct columnSpec{
		columnSpec(std::string lab, std::string attr, bool canWrap=false):
		label(lab),attribute(attr),allowWrap(canWrap){}
		
		std::string label;
		std::string attribute;
		bool allowWrap;
	};

	struct ProgressManager{
	private:
	  bool stop_;
	  std::atomic<bool> showingProgress_;
	  std::atomic<bool> actuallyShowingProgress_;
	  unsigned int nestingLevel;
	  float progress_;
	  std::mutex mut_;
	  std::condition_variable cond_;
	  std::thread thread_;
	  std::chrono::system_clock::time_point progressStart_;
	  struct WorkItem{
	    std::chrono::system_clock::time_point time_;
	    std::function<void()> work_;
	    WorkItem(){}
	    WorkItem(std::chrono::system_clock::time_point t, std::function<void()> w);
	    bool operator<(const WorkItem&) const;
	  };
	  std::priority_queue<WorkItem> work_;
	  bool repeatWork_;

	  void start_scan_progress(std::string msg);
	  void scan_progress(int progress);
	  void show_progress();
	public:
	  std::atomic<bool> verbose_;
	  
	  explicit ProgressManager();
	  ~ProgressManager();
    
	  void MaybeStartShowingProgress(std::string message);
	  ///\param value a fraction in [0,1]
	  void SetProgress(float value);
	  void ShowSomeProgress();
	  void StopShowingProgress();
	};
	
	///The progress bar manager
	ProgressManager pman_;
	
	struct ProgressToken{
		ProgressManager& pman;
		ProgressToken(ProgressManager& pman, const std::string& msg):pman(pman){
			start(msg);
		}
		~ProgressToken(){ end(); }
		void start(const std::string& msg){
			pman.MaybeStartShowingProgress(msg);
			pman.ShowSomeProgress();
		}
		void end(){ pman.StopShowingProgress(); }
	};
	struct HideProgress{
		ProgressManager& pman;
		bool orig;
		HideProgress(ProgressManager& pman):pman(pman),orig(pman.verbose_){
			pman.verbose_=false;
		}
		~HideProgress(){ pman.verbose_=orig; }
	};

	void showError(const std::string& maybeJSON);
	
	std::string formatTable(const std::vector<std::vector<std::string>>& items,
	                        const std::vector<columnSpec>& columns,
				const bool headers) const;
	
	std::string jsonListToTable(const rapidjson::Value& jdata,
	                            const std::vector<columnSpec>& columns,
				    const bool headers) const;

	std::string displayContents(const rapidjson::Value& jdata,
				    const std::vector<columnSpec>& columns,
				    const bool headers) const;
	
	std::string formatOutput(const rapidjson::Value& jdata, const rapidjson::Value& original,
				 const std::vector<columnSpec>& columns) const;
	
	///return true if the argument mtaches the correct format for an instance ID
	static bool verifyInstanceID(const std::string& id);
	///return true if the argument mtaches the correct format for a secret ID
	static bool verifySecretID(const std::string& id);
	
	static void filterInstanceNames(rapidjson::Document& json, std::string pointer);
	
	std::string endpointPath;
	std::string apiEndpoint;
	std::string apiVersion;
	std::string credentialPath;
	std::string token;
	bool useANSICodes;
	std::size_t outputWidth;
	std::string outputFormat;
#ifdef USE_CURLOPT_CAINFO
	std::string caBundlePath;
#endif
	
	friend void registerCommonOptions(CLI::App&, Client&);
};

#endif
