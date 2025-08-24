#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <iostream>
#include <vector>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include <functional>
#include <sys/mount.h>
#include <csignal>
#include <sys/stat.h>
#include <fstream>
#include <stdlib.h>
using namespace std;

void run_command(const std::string& cmd) {
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Command failed: " << cmd << std::endl;
    }
}
int child_func(void* arg) {
    
    sleep(1); // sleep for 1 second to allow the parent process to set up the network namespaces and veths
    // sethostname("container", 9); 
    cout<<"we are inside the child process with PID: " << getpid() << endl;

    run_command("ip link set lo up") ; // bring up the loopback interface
    run_command("ip link set veth1 up"); 
    run_command("ip addr add 10.0.0.2/24 dev veth1"); 
    run_command("ip route add default via 10.0.0.1");


    const char* new_root = "/tmp/my_root";
    string proc_path = string(new_root) + "/proc";
    mkdir(proc_path.c_str(), 0755); // create the new root directory with permissions
    if (mount("proc", proc_path.c_str(), "proc", 0, NULL) == -1) {
        perror("mount /proc failed");
        return 1;
    }
    if (chroot(new_root) == -1) {
        perror("chroot failed");
        return 1;
    }
    chdir("/");
    // we can now execute the command in a new PID namespace
        // we need to cast the argument to a char** to use execvp
        sethostname("container",9);
        char** args = static_cast<char**>(arg);
        // we can now execute the command in a new PID namespace
        if(execv(args[0], args)==-1) {
            // if execvp fails, we should return an error code
            return 1;
            perror("execv failed"); // what is perrror? it prints the last error that occurred
        }
        return 0;
}

void setup_cgroup(pid_t child_pid) {
    const char* cgroup_path = "/sys/fs/cgroup/memory/my_container";
    mkdir(cgroup_path, 0755);   

    // Set memory limit to 100 MB
    std::ofstream mem_limit_file(std::string(cgroup_path) + "/memory.limit_in_bytes");
    if (mem_limit_file.is_open()) {
        mem_limit_file << 100 * 1024 * 1024;
        mem_limit_file.close();
    } else {
        std::cerr << "Failed to open memory.limit_in_bytes" << std::endl;
        return;
    }

    // Add the child process to the cgroup
    std::ofstream procs_file(std::string(cgroup_path) + "/cgroup.procs");
    if (procs_file.is_open()) {
        procs_file << child_pid;
        procs_file.close();
    } else {
        std::cerr << "Failed to open cgroup.procs" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    if(argc <=1) {
        std::cerr << "Usage: " << argv[0] << " <command> [args...]" << std::endl;
        return 1;
    }
    run_command("mkdir -p /tmp/my_root/bin /tmp/my_root/proc /tmp/my_root/lib /tmp/my_root/lib64 /tmp/my_root/usr/bin");


    // A list of essential binaries for our container
    const std::vector<std::string> bins = {"/bin/bash", "/bin/ps", "/bin/hostname", "/bin/ls", "/bin/ip", "/bin/ping" };

    for (const auto& bin : bins) {
        // Copy the binary itself
        run_command("cp " + bin + " /tmp/my_root/bin/");

        // Create a command to find and copy all shared library dependencies
        std::string ldd_cmd = "ldd " + bin + " | grep '=> /' | awk '{print $3}' | xargs -I '{}' cp '{}' /tmp/my_root/lib/";
        run_command(ldd_cmd);
    }

    run_command("cp /lib64/ld-linux-x86-64.so.2 /tmp/my_root/lib64/");
    system("cp /usr/bin/python3 /tmp/my_root/usr/bin/");
    run_command("mkdir -p /tmp/my_root/etc");
    run_command("cp /etc/resolv.conf /tmp/my_root/etc/");

    // so memory space is not automatically allocated when using clones
    // why use clone over fork, now because we now will be able to set up namespaces
    const int STACK_SIZE = 1024 * 1024; // 1 MB stack size
    vector<char> child_stack(STACK_SIZE);
    char* stack_top  = child_stack.data() + STACK_SIZE;
    // the stack grown downwards in linux, so while we are passing the adress in clone, we need to pass the top of the stack so it grows down
    int clone_flags = CLONE_NEWPID | SIGCHLD | CLONE_NEWNS | CLONE_NEWNET ;
    cout<<"Creating a new container with PID namespace..." << endl;
    pid_t child_pid = clone(child_func, stack_top, clone_flags, &argv[1]);
    if(child_pid == -1) {
        perror("clone failed");
        return 1;
    }
    setup_cgroup(child_pid);
    cout << "Child process created with PID: " << child_pid << endl;
    
    // we add sleep(1) in our child func, so that we setup our network namespaces and setup the veths and bridge before we start the child process
    run_command("ip link add veth0 type veth peer name veth1");
    run_command("ip link set veth1 netns " + std::to_string(child_pid));
    run_command("brctl addbr bridge");
    run_command("brctl addif bridge veth0");
    run_command("ip link set veth0 up");
    run_command("ip link set bridge up");
    run_command("ip addr add 10.0.0.1/24 dev bridge");
    // Allow traffic from our bridge to the main network interface
    run_command("iptables -A FORWARD -i bridge -j ACCEPT");

    // Allow established traffic to come back from the main interface to our bridge
    run_command("iptables -A FORWARD -o bridge -m state --state RELATED,ESTABLISHED -j ACCEPT");
    // wait for the child process to finish
    int status;
    waitpid(child_pid, &status, 0); // the arguments are the PID of the child process, a pointer to an integer where the exit status will be stored, and options (0 means no options)
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        cout << "Child killed by signal " << sig;
        if (sig == 9) cout << " (SIGKILL - likely OOM)";
        cout << endl;
    } else if (WIFEXITED(status)) {
        cout << "Child exited with code " << WEXITSTATUS(status) << endl;
    }
    system("umount /tmp/my_root/proc");
    system("rmdir /sys/fs/cgroup/memory/my_container");
    // system("rm -rf /tmp/my_root");
    cout << "Child process finished." << endl;
    return 0;
}
