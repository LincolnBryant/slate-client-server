#include "Entities.h"

#include <boost/lexical_cast.hpp>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>

bool operator==(const User& u1, const User& u2){
	return(u1.valid==u2.valid && u1.id==u2.id);
}
bool operator!=(const User& u1, const User& u2){
	return(!(u1==u2));
}

std::ostream& operator<<(std::ostream& os, const User& u){
	if(!u)
		return os << "invalid user";
	os << u.id;
	if(!u.name.empty())
		os << " (" << u.name << ')';
	return os;
}

bool operator==(const Group& v1, const Group& v2){
	return v1.id==v2.id;
}

std::ostream& operator<<(std::ostream& os, const Group& group){
	if(!group)
		return os << "invalid Group";
	os << group.id;
	if(!group.name.empty())
		os << " (" << group.name << ')';
	return os;
}

bool operator==(const Cluster& c1, const Cluster& c2){
	return c1.id==c2.id;
}

std::ostream& operator<<(std::ostream& os, const Cluster& c){
	if(!c)
		return os << "invalid cluster";
	os << c.id;
	if(!c.name.empty())
		os << " (" << c.name << ')';
	return os;
}

std::ostream& operator<<(std::ostream& os, const Application& a){
	if(!a)
		return os << "invalid application";
	os << a.name;
	return os;
}

bool operator==(const ApplicationInstance& i1, const ApplicationInstance& i2){
	return i1.id==i2.id;
}

std::ostream& operator<<(std::ostream& os, const ApplicationInstance& a){
	if(!a)
		return os << "invalid application instance";
	os << a.id;
	if(!a.name.empty())
		os << " (" << a.name << ')';
	return os;
}

bool operator==(const Secret& s1, const Secret& s2){
	return s1.id==s2.id;
}

std::ostream& operator<<(std::ostream& os, const Secret& s){
	if(!s)
		return os << "invalid secret";
	os << s.id;
	if(!s.name.empty())
		os << " (" << s.name << ')';
	return os;
}

std::ostream& operator<<(std::ostream& os, const GeoLocation& gl){
	return os << gl.lat << ',' << gl.lon;
}

std::istream& operator>>(std::istream& is, GeoLocation& gl){
	is >> gl.lat;
	if(!is)
		return is;
	char comma=0;
	is >> comma;
	if(comma!=','){
		is.setstate(is.rdstate()|std::ios::failbit);
		return is;
	}
	is >> gl.lon;
	return is;
}

const std::string IDGenerator::userIDPrefix="user_";
const std::string IDGenerator::clusterIDPrefix="cluster_";
const std::string IDGenerator::groupIDPrefix="group_";
const std::string IDGenerator::instanceIDPrefix="instance_";
const std::string IDGenerator::secretIDPrefix="secret_";

std::string IDGenerator::generateRawID(){
	uint64_t value;
	{
		std::lock_guard<std::mutex> lock(mut);
		value=std::uniform_int_distribution<uint64_t>()(idSource);
	}
	std::ostringstream os;
	using namespace boost::archive::iterators;
	using base64_text=base64_from_binary<transform_width<const unsigned char*,6,8>>;
	std::copy(base64_text((char*)&value),base64_text((char*)&value+sizeof(value)),ostream_iterator<char>(os));
	std::string result=os.str();
	//convert to RFC 4648 URL- and filename-safe base64
	for(char& c : result){
		if(c=='+') c='-';
		if(c=='/') c='_';
	}
	return result;
}
