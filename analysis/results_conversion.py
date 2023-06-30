import joint_distribution_pb2
import distbench_pb2
import traffic_config_pb2
import os
import argparse

# This a script to process the results from the distbench results proto format
# with the purpose of putting the results into a text file

class Formatter:
    def __init__(self, supress_header, consider_warmups):
        self.supress_header = supress_header
        self.consider_warmups = consider_warmups

    def format_file(self) -> list:
        pass

    def summarize(self) -> str:
        pass

class DefaultFormatter(Formatter):
    # this function takes a list of rpc and converts them into an intermediate format
    def summarize(self, rpc_list):
        summary = []
        for rpc in rpc_list:
            if(self.consider_warmups or not rpc.warmup):
                summary.append(tuple([rpc.request_size, rpc.latency_ns]))
        return summary

    # this function takes datasets in the intermediate format, possibly a concatenation
    # of high level summaries, and converts them into a string which can be written into a file
    def format_file(self, summary):
        output_str = ""
        if(not self.supress_header):
            output_str += "{: <15} {: <15}\n".format("Request_size", "Latency_ns")
        for item in summary:
            output_str += ("{: <15} {: <15}\n".format(item[0], item[1]))
        return output_str

class TestProcessor:
    def __init__(self, test_proto_message, output_formatter):
        # test_proto_message is a protobuf message of type TestResult
        self.test_proto_message = test_proto_message
        # map from services names to the number of instances they have
        self.services_count = {}
        # list of services that act as servers
        self.server_services = set()
        # list of services that act as clients
        self.client_services = set()
        # rpcs list stores the names of the rpcs in the order the traffic config stores the rpcs
        self.rpc_names = []
        self.output_formatter = output_formatter
        # iterate through the services to get the cumber of instances per each one
        for service in self.test_proto_message.traffic_config.services:
                self.services_count[service.name] = service.count

        for rpc in self.test_proto_message.traffic_config.rpc_descriptions:
            # get the services that act as servers and the ones that act as clients
            self.client_services.add(rpc.client)
            self.server_services.add(rpc.server)
            # get the rpc names
            self.rpc_names.append(rpc.name)

    def create_directory_tree(self, output_directory):
        test_summary_file_path = os.path.join(output_directory, "test_summary.txt")
        self.write_summary(self.get_summary(), test_summary_file_path)
        for client in self.client_services:
            self.create_directory_tree_for_client(client, output_directory)

    def create_directory_tree_for_client(self, client, output_directory):
        client_directory_path = os.path.join(output_directory, client)
        try:
            os.mkdir(client_directory_path)
        except:
            print("Error creating %s directory" % client_directory_path)
            exit()
        # write the summary for the client
        client_summary_file_path = os.path.join(client_directory_path, "client_summary.txt")
        self.write_summary(self.get_summary_per_client(client), client_summary_file_path)
        # create directory for each instance of the client
        for i in range(self.services_count[client]):
            # obtain the full name of the client instance
            client_instance = os.path.join(client, str(i))
            client_instance_directory_path = os.path.join(client_directory_path, str(i))
            try:
                os.mkdir(client_instance_directory_path)
            except:
                print("Error creating %s directory" % client_instance_directory_path)
                exit()
            # write the summary for the client instance
            instance_summary_file_path = os.path.join(client_instance_directory_path, "client_instance_summary.txt")
            self.write_summary(self.get_summary_per_client_instance(client_instance), instance_summary_file_path)
            for service in self.server_services:
                    self.create_directory_tree_for_service(client_instance, service, client_instance_directory_path)

    def create_directory_tree_for_service(self, client_instance, service, output_directory):
        service_directory_path = os.path.join(output_directory, service)
        try:
            os.mkdir(service_directory_path)
        except:
            print("Error creating %s directory" % service_directory_path)
            exit()
        # write the summary for the service
        service_summary_file_path = os.path.join(service_directory_path, "service_summary.txt")
        self.write_summary(self.get_summary_per_service(client_instance, service), service_summary_file_path)
        # create directory for each instance of the service
        for i in range(self.services_count[service]):
            # obtain the full name of the service instance
            server_instance_directory_path = os.path.join(service_directory_path, str(i))
            server_instance = os.path.join(service, str(i))
            try:
                os.mkdir(server_instance_directory_path)
            except:
                print("Error creating %s directory" % server_instance_directory_path)
                exit()
            # write the logs for these instances
            self.write_pairwise_logs(client_instance, server_instance, server_instance_directory_path)

    def write_pairwise_logs(self, client_instance, service_instance, output_directory):
        client_instance_logs = self.test_proto_message.service_logs.instance_logs[client_instance]
        service_instance_logs = client_instance_logs.peer_logs[service_instance]
        for rpc_index in service_instance_logs.rpc_logs.keys():
            summary_per_rpc = self.get_summary_per_rpc_per_pair_of_instances(client_instance, service_instance, rpc_index)
            rpc_summary_file_path = os.path.join(output_directory, self.rpc_names[rpc_index] + ".txt")
            self.write_summary(summary_per_rpc, rpc_summary_file_path)
        summary_per_instances = self.get_summary_per_service_instance(client_instance, service_instance)
        path_server_instance = os.path.join(output_directory, "pairwise_summary.txt")
        self.write_summary(summary_per_instances, path_server_instance)

    # gets the summary of a single rpc sent by a client_instance and received by a server_instance
    def get_summary_per_rpc_per_pair_of_instances(self, client_instance, service_instance, rpc_index):
        client_instance_logs = self.test_proto_message.service_logs.instance_logs[client_instance]
        service_instance_logs = client_instance_logs.peer_logs[service_instance]
        return self.output_formatter.summarize(service_instance_logs.rpc_logs[rpc_index].successful_rpc_samples)

    # gets the summary of all the rpcs sent by a single client_instance to a single server_instance
    def get_summary_per_service_instance(self, client_instance, service_instance):
        client_instance_logs = self.test_proto_message.service_logs.instance_logs[client_instance]
        server_instance_logs = client_instance_logs.peer_logs[service_instance]
        summary = []
        for i in server_instance_logs.rpc_logs.keys():
            summary += self.get_summary_per_rpc_per_pair_of_instances(client_instance, service_instance, i)
        return summary

    # gets the summary of all the rpc sent from a client instance to all the instances of a service
    def get_summary_per_service(self, client_instance, service):
        client_instance_logs = self.test_proto_message.service_logs.instance_logs[client_instance]
        summary = []
        for service_instance in client_instance_logs.peer_logs.keys():
            # gets the server name from the client instance
            service_name = service_instance.rsplit("/", 1)[0]
            if(service_name == service):
                summary += self.get_summary_per_service_instance(client_instance, service_instance)
        return summary

    def get_summary_per_client_instance(self, client_instance):
        client_instance_logs = self.test_proto_message.service_logs.instance_logs[client_instance]
        summary = []
        for server_instance in client_instance_logs.peer_logs.keys():
            summary += self.get_summary_per_service_instance(client_instance, server_instance)
        return summary

    def get_summary_per_client(self, client):
        summary = []
        for client_instance in self.test_proto_message.service_logs.instance_logs.keys():
            # get the client name from the client instance
            client_name = client_instance.rsplit("/", 1)[0]
            if(client_name == client):
                summary += self.get_summary_per_client_instance(client_instance)
        return summary

    def get_summary(self):
        summary = []
        for client_instance in self.test_proto_message.service_logs.instance_logs.keys():
            summary += self.get_summary_per_client_instance(client_instance)
        return summary

    def write_summary(self, summary, path):
        if(summary == []):
            return
        try:
            fs = open(path,"w")
        except:
            print("error trying to open file %s" % path)
            exit()
        fs.write(self.output_formatter.format_file(summary))
        fs.close()


class DistbenchResultsIO:
    def __init__(self, input_file, output_directory, output_formatter):
        self.input_file = input_file
        self.output_directory = output_directory
        self.tests_results = []
        self.output_formatter = output_formatter

    def parse_from_binary(self):
        test_sequence = distbench_pb2.TestSequenceResults()
        try:
            with open(self.input_file, 'rb') as f:
                var = f.read()
                test_sequence.ParseFromString(var)
                self.tests_results = test_sequence.test_results
        except:
            print("Error parsing the file %s" % self.input_file)
            exit()

    def create_all_test_directories(self):
        self.parse_from_binary()
        for i, test in enumerate(self.tests_results):
            test_dir = self.output_directory + "/test_" + str(i)
            try:
                os.mkdir(test_dir)
            except:
                print("Error creating directory %s" % test_dir)
            t = TestProcessor(test, self.output_formatter)
            t.create_directory_tree(test_dir)

    def write_overall_summary(self):
        overall_summary = []
        for test in self.tests_results:
            t = TestProcessor(test, self.output_formatter)
            overall_summary += t.get_summary()
        try:
            fs = open(self.output_directory + "/overall_summary.txt","w")
        except:
            print("Error opening the file %s" % self.output_directory)
            exit()
        fs.write(self.output_formatter.format_file(overall_summary))
        fs.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--output_directory', type = str, required = True)
    parser.add_argument('--input_file', type = str, required = True)
    parser.add_argument('--output_format', type = str, default = "default", required = False)
    parser.add_argument('--supress_header', default=False, action='store_true', required = False)
    parser.add_argument('--consider_warmups', default=False, action='store_true', required = False)
    args = parser.parse_args()
    directory = args.output_directory
    file_name = args.input_file
    output_format = args.output_format
    supress_header = args.supress_header
    consider_warmups = args.consider_warmups
    if(output_format == "default"):
        formatter = DefaultFormatter(supress_header, consider_warmups)
    else:
        print("Output format %s not supported" % output_format)
        exit()
    d = DistbenchResultsIO(file_name, directory, formatter)
    d.create_all_test_directories()
    d.write_overall_summary()