#include "test.h"

#include <set>
#include <utility>

#include <ServerUtilities.h>

TEST(UnauthenticatedRemoveClusterAllowedGroup){
	using namespace httpRequests;
	TestContext tc;
	
	//try removing an allowed Group with no authentication
	auto remResp=httpDelete(tc.getAPIServerURL()+
	                     "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to revoke a Group's access to a cluster without "
	             "authentication should be rejected");
	
	//try removing an allowed Group with invalid authentication
	remResp=httpDelete(tc.getAPIServerURL()+
	                "/"+currentAPIVersion+"/clusters/some-cluster/allowed_groups/some-group"
	                "?token=00112233-4455-6677-8899-aabbccddeeff");
	ENSURE_EQUAL(remResp.status,403,
	             "Requests to revoke a Group's access to a cluster with invalid "
	             "authentication should be rejected");
}

TEST(RemoveGroupAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="group-access-deny-owning-group";
	std::string groupName2="group-access-deny-guest-group";
	
	std::string groupID1;
	{ //add a Group to register a cluster with
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID1=groupData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID2=groupData["metadata"]["id"].GetString();
	}
	
	{ //grant the new Group access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_groups/"+groupID2+"?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
	}
	
	{ //list the groups which can use the cluster
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),2,"Two groups should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair(groupID1,groupName1)),"Owning Group should still have access");
		ENSURE(groups.count(std::make_pair(groupID2,groupName2)),"Additional Group should have access");
	}
	
	{ //remove the new Group's access to the cluster again
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/"+groupID2+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,200, "Group access removal request should succeed: "+revokeResp.body);
	}
	
	{ //list the groups which can use the cluster again
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One Group should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair(groupID1,groupName1)),"Owning Group should still have access");
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		if(instResp.status==200){
			rapidjson::Document data;
			data.Parse(instResp.body);
			instID=data["metadata"]["id"].GetString();
		}
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail after access is removed");
	}
}

TEST(RemoveUniversalAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="universal-access-deny-owning-group";
	std::string groupName2="universal-access-deny-guest-group";
	
	std::string groupID1;
	{ //add a Group to register a cluster with
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID1=groupData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID2=groupData["metadata"]["id"].GetString();
	}
	
	{ //grant all groups access to the cluster
		auto accessResp=httpPut(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								"/allowed_groups/*?token="+adminKey,"");
		ENSURE_EQUAL(accessResp.status,200, "Group access grant request should succeed: "+accessResp.body);
	}
	
	{ //list the groups which can use the cluster
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One pseudo-Group should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair("*","<all>")),"All groups should have access");
	}
	
	{ //remove non-owning groups' access to the cluster again
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/*?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,200, "Group access removal request should succeed: "+revokeResp.body);
	}
	
	{ //list the groups which can use the cluster again
		auto listResp=httpGet(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
		                      "/allowed_groups?token="+adminKey);
		ENSURE_EQUAL(listResp.status,200, "Group access list request should succeed");
		ENSURE(!listResp.body.empty());
		rapidjson::Document listData;
		listData.Parse(listResp.body);
		std::cout << listResp.body << std::endl;
		ENSURE_EQUAL(listData["items"].Size(),1,"One Group should now have access to the cluster");
		std::set<std::pair<std::string,std::string>> groups;
		for(const auto& item : listData["items"].GetArray())
			groups.emplace(item["metadata"]["id"].GetString(),item["metadata"]["name"].GetString());
		ENSURE(groups.count(std::make_pair(groupID1,groupName1)),"Owning Group should still have access");
	}
	
	std::string instID;
	struct cleanupHelper{
		TestContext& tc;
		const std::string& id, key;
		cleanupHelper(TestContext& tc, const std::string& id, const std::string& key):
		tc(tc),id(id),key(key){}
		~cleanupHelper(){
			if(!id.empty())
				auto delResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/instances/"+id+"?token="+key);
		}
	} cleanup(tc,instID,adminKey);
	{ //test installing an application
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		request.AddMember("group", groupName2, alloc);
		request.AddMember("cluster", clusterID, alloc);
		request.AddMember("tag", "install1", alloc);
		request.AddMember("configuration", "", alloc);
		auto instResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/apps/test-app?test&token="+adminKey,to_string(request));
		if(instResp.status==200){
			rapidjson::Document data;
			data.Parse(instResp.body);
			instID=data["metadata"]["id"].GetString();
		}
		ENSURE_EQUAL(instResp.status,403,"Application install request should fail after access is removed");
	}
}

TEST(MalformedRevokeGroupAccessToCluster){
	using namespace httpRequests;
	TestContext tc;
	
	std::string adminKey=getPortalToken();
	std::string groupName1="owning-group";
	std::string groupName2="guest-group";
	
	{ //attempt to revoke access to a cluster which does not exist
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+"nonexistent-cluster"+
								   "/allowed_groups/"+groupName2+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,404, 
		             "Request to revoke access to a nonexistent cluster should be rejected");
	}
	
	std::string groupID1;
	{ //add a Group to register a cluster with
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName1, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+adminKey,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID1=groupData["metadata"]["id"].GetString();
	}
	
	std::string clusterID;
	{ //register a cluster
		auto kubeConfig=tc.getKubeConfig();
		rapidjson::Document request1(rapidjson::kObjectType);
		auto& alloc = request1.GetAllocator();
		request1.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "testcluster", alloc);
		metadata.AddMember("group", groupID1, alloc);
		metadata.AddMember("owningOrganization", "Department of Labor", alloc);
		metadata.AddMember("kubeconfig", rapidjson::StringRef(kubeConfig), alloc);
		request1.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters?token="+adminKey, 
		                         to_string(request1));
		ENSURE_EQUAL(createResp.status,200, "Cluster creation should succeed");
		ENSURE(!createResp.body.empty());
		rapidjson::Document clusterData;
		clusterData.Parse(createResp.body);
		clusterID=clusterData["metadata"]["id"].GetString();
	}
	
	{ //attempt to revoke access for a Group which does not exist
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/"+"nonexistent-group"+"?token="+adminKey);
		ENSURE_EQUAL(revokeResp.status,404, 
		             "Request to revoke access for a nonexistent Group should be rejected");
	}
	
	std::string tok;
	{ //create a user which does not belong to the owning VO
		rapidjson::Document request(rapidjson::kObjectType);
		auto& alloc = request.GetAllocator();
		request.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", "Bob", alloc);
		metadata.AddMember("email", "bob@place.com", alloc);
		metadata.AddMember("phone", "555-5555", alloc);
		metadata.AddMember("institution", "Center of the Earth University", alloc);
		metadata.AddMember("admin", false, alloc);
		metadata.AddMember("globusID", "Bob's Globus ID", alloc);
		request.AddMember("metadata", metadata, alloc);
		auto createResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/users?token="+adminKey,to_string(request));
		ENSURE_EQUAL(createResp.status,200,"User creation request should succeed");
		rapidjson::Document createData;
		createData.Parse(createResp.body);
		tok=createData["metadata"]["access_token"].GetString();
	}
	
	std::string groupID2;
	{ //add another Group to give access to the cluster
		rapidjson::Document createGroup(rapidjson::kObjectType);
		auto& alloc = createGroup.GetAllocator();
		createGroup.AddMember("apiVersion", currentAPIVersion, alloc);
		rapidjson::Value metadata(rapidjson::kObjectType);
		metadata.AddMember("name", groupName2, alloc);
		metadata.AddMember("scienceField", "Logic", alloc);
		createGroup.AddMember("metadata", metadata, alloc);
		auto groupResp=httpPost(tc.getAPIServerURL()+"/"+currentAPIVersion+"/groups?token="+tok,
							 to_string(createGroup));
		ENSURE_EQUAL(groupResp.status,200, "Group creation request should succeed");
		ENSURE(!groupResp.body.empty());
		rapidjson::Document groupData;
		groupData.Parse(groupResp.body);
		groupID2=groupData["metadata"]["id"].GetString();
	}
	
	{ //have the non-owning user attempt to revoke access
		auto revokeResp=httpDelete(tc.getAPIServerURL()+"/"+currentAPIVersion+"/clusters/"+clusterID+
								   "/allowed_groups/"+groupID2+"?token="+tok);
		ENSURE_EQUAL(revokeResp.status,403, 
		             "Request to revoke access by a non-member of the owning Group should be rejected");
	}
}
