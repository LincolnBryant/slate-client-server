#ifndef SLATE_CLUSTER_COMMANDS_H
#define SLATE_CLUSTER_COMMANDS_H

#include "crow.h"
#include "Entities.h"
#include "PersistentStore.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

///List currently known clusters
crow::response listClusters(PersistentStore& store, const crow::request& req);
///Register a new cluster
crow::response createCluster(PersistentStore& store, const crow::request& req);
///Delete a cluster
///\param clusterID the cluster to destroy
crow::response deleteCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);
///Update a cluster's informaation
///\param clusterID the cluster to update
crow::response updateCluster(PersistentStore& store, const crow::request& req, 
                             const std::string& clusterID);

#endif //SLATE_CLUSTER_COMMANDS_H
