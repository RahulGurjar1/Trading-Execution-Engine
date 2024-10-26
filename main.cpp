#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>

using namespace std;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class DeribitAPI {
private:
    string access_token;
    string refresh_token;
    string base_url = "https://test.deribit.com/api/v2/";
    CURL* curl;
    int request_id = 1;

    // Function to authenticate with the API and obtain access token and refresh token
    bool authenticate() {
        string url = base_url + "public/auth";//url construction for authentication request
        string response; // initialized to store the response from the API call.

        curl_easy_reset(curl); // resets the curl handle to ensure no interference from previous requests
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());// sets the URL for the request

        // Creating a JSON-RPC request
        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["id"] = request_id++;
        request["method"] = "public/auth";
        request["params"]["grant_type"] = "client_credentials";
        request["params"]["client_id"] = client_id;
        request["params"]["client_secret"] = client_secret;

        // Convert JSON request to string
        Json::FastWriter writer;
        string request_str = writer.write(request);

        // Setting up the request headers used in the CURL request
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Cunfiguring the CURL options
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // Performing the CURL request and freeing the headers
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        // Checking for errors in the CURL request
        if (res != CURLE_OK) {
            return false;
        }

        
        Json::Value root;
        Json::Reader reader; // object to parse response string to JSON

        // Parsing the response and extracting the access token and refresh token
        if (reader.parse(response, root) && root.isMember("result")) {
            access_token = root["result"]["access_token"].asString();
            refresh_token = root["result"]["refresh_token"].asString();
            return true;
        }

        return false;
    }

    // Function to make a request to the API and handle authentication and managing the response
    // Takes the API endpoint and parameters as input and returns the response string
    string makeRequest(const string& endpoint, const Json::Value& params) {
        // Before making a request to private endpoints, check if access token is present
        // if not, authenticate to obtain a new access token
        if (access_token.empty() && endpoint.find("private/") != string::npos) {
            if (!authenticate()) {
                throw runtime_error("Failed to authenticate");
            }
        }

        // Construct the URL for the request
        string url = base_url + endpoint;
        string response;

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());// sets the URL for the request

        // Creating a JSON-RPC request
        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["id"] = request_id++;
        request["method"] = endpoint;
        request["params"] = params;

        // Convert JSON request to string
        Json::FastWriter writer;
        string request_str = writer.write(request);

        // Setting up the HTTP request headers

        struct curl_slist* headers = nullptr;
        if (endpoint.find("private/") != string::npos) {
            string auth_header = "Authorization: Bearer " + access_token;
            headers = curl_slist_append(headers, auth_header.c_str());
        }
        headers = curl_slist_append(headers, "Content-Type: application/json");

        // Configuring the CURL options
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // Performing the CURL request and freeing the headers
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        // Checking for errors in the CURL request
        if (res != CURLE_OK) {
            throw runtime_error("CURL request failed: " + string(curl_easy_strerror(res)));
        }

        // Parse the response and check for authentication errors
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(response, root) && root.isMember("error")) {
            if (root["error"]["code"].asInt() == 13009) {  // Authentication error
                if (!authenticate()) {
                    throw runtime_error("Failed to re-authenticate");
                }
                return makeRequest(endpoint, params);  // Retry with new token
            }
        }

        return response;
    }

    // Helper function to validate amount against contract size
    double validateAmount(double amount) {
        double contract_size = 10.0;
        if (fmod(amount, contract_size) != 0) {
            throw runtime_error("Amount must be a multiple of contract size");
        }
        return amount;
    }

public:
    string client_id;
    string client_secret;

    DeribitAPI(const string& id, const string& secret) 
    {
        client_id=id;
        client_secret=secret;
        curl = curl_easy_init(); // Initialize CURL
        if (!curl) {
            throw runtime_error("Failed to initialize CURL");
        }
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Set 10 sec timeout for CURL requests
    }

    // Destructor to cleanup CURL handle when object of DERIBITAPI goes out of scope
    ~DeribitAPI() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    // Function to get the orderbook for a given instrument
    Json::Value getOrderbook(const string& instrument) {
        Json::Value params;
        params["instrument_name"] = instrument;
        params["depth"] = 5;
        string response = makeRequest("public/get_order_book", params);

        Json::Value root;
        Json::Reader reader;
        reader.parse(response, root);
        return root;
    }

    
    // Function to place an order on the exchange
    Json::Value placeOrder(const string& instrument, const string& side, double amount, double price) {
        Json::Value params;
        params["instrument_name"] = instrument;
        params["amount"] = validateAmount(amount);
        params["price"] = price;
        params["type"] = "limit";
        params["post_only"] = true;

        // Construct the endpoint based on the side
        string endpoint = "private/" + side;

        try {
            string response = makeRequest(endpoint, params);// make request to the API endpoint based on the parameters
            Json::Value root;
            Json::Reader reader;
            reader.parse(response, root);
            if (root.isMember("error")) {
                throw runtime_error(root["error"]["message"].asString());
            }
            return root;
        } catch (const runtime_error& e) {
            cerr << "Order placement failed: " << e.what() << endl;
        }
        return Json::Value();
    }

    // Function to modify an existing order on the exchange
    Json::Value modifyOrder(const string& order_id, double new_price, double amount) {
        Json::Value params;
        params["order_id"] = order_id;
        params["price"] = new_price;
        params["amount"] = validateAmount(amount); // To ensure the amount is valid

        string response = makeRequest("private/edit", params);

        Json::Value root;
        Json::Reader reader;
        reader.parse(response, root);
        return root;
    }

    // Function to cancel an existing order
    Json::Value cancelOrder(const string& order_id) {
        Json::Value params;
        params["order_id"] = order_id;
        string response = makeRequest("private/cancel", params);

        Json::Value root;
        Json::Reader reader;
        reader.parse(response, root);
        return root;
    }

    // Function to get the positions for a given currency
    Json::Value getPositions(const string& currency) {
        Json::Value params;
        params["currency"] = currency;
        string response = makeRequest("private/get_positions", params);

        Json::Value root;
        Json::Reader reader;
        reader.parse(response, root);
        return root;
    }
};

int main() {
    try {
        //Initialize the API object with client ID and client secret
        DeribitAPI api("hh7Hpy35", "cJKqbq_Vg9L78gRnd46619V5F0TkeVEbj2tCHrMqCik");

        string operation="";
        string orderId = "";
        while(operation!="exit"){
            cin>>operation;
            if(operation=="getOrderbook"){
                Json::Value orderbook = api.getOrderbook("BTC-PERPETUAL");
                cout << "Orderbook: " << orderbook.toStyledString() << endl;
            }
            else if(operation=="placeOrder"){
                // Price and other parameters can also be taken as input from the user
                // I am just demonstrating the working
                Json::Value order = api.placeOrder("BTC-PERPETUAL", "buy", 10, 25000.0);
                if (order.isMember("error")) {
                    cerr << "Failed to place order: " << order["error"]["message"].asString() << endl;
                }
                else{
                    cout<<"Order placed: "<<order.toStyledString()<<endl;
                    orderId = order["result"]["order"]["order_id"].asString();
                }
            }
            else if(operation=="modifyOrder"){
                if(orderId==""){
                    cout<<"Place a order to modify"<<endl;
                    continue;
                }
                double newPrice = 26000;
                double newAmount = 5;
                cin>>newPrice>>newAmount;

                Json::Value modifyResponse = api.modifyOrder(orderId, newPrice, newAmount);

                // Check for errors in modification
                if (modifyResponse.isMember("error")) {
                    cerr << "Failed to modify order: " << modifyResponse["error"]["message"].asString() << endl;
                } 
                else {
                    cout << "Order modified successfully." << endl;
                }
            }
            else if(operation=="cancelOrder"){
                if(orderId==""){
                    cout<<"Place a order to modify"<<endl;
                    continue;
                }
                auto cancelResponse = api.cancelOrder(orderId);
                cout << "Order canceled: " << cancelResponse << endl;

                // Check for errors in cancellation
                if (cancelResponse.isMember("error")) {
                    cerr << "Failed to cancel order: " << cancelResponse["error"]["message"].asString() << endl;
                } 
                else {
                    cout << "Order canceled successfully." << endl;
                }
            }
            else if(operation=="getPositions"){
                Json::Value positions = api.getPositions("BTC");
                cout << "Positions: " << positions.toStyledString() << endl;
            }
            else if(operation=="exit"){
                cout<<"program Exited"<<endl;
                exit(0);
            }
            else{
                cout<<"Invalid operation"<<endl;
            }
        }

        
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}