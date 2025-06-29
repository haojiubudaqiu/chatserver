#include "json.hpp"
using json = nlohmann ::json;

#include <iostream>
#include <string>
#include<vector>
#include<map>
using namespace std;

//json 序列化示例1
void func1()
{
    json js;
    js["msg_type"] =2;
    js["fron"]="zhang san";
    js["to"]="li si";
    js["msg"]="hello,what are you doing now?";

    string  sendBuf=js.dump();

    cout<<sendBuf.c_str()<<endl;
}


int main()
{   
    func1();
    return 0;
}