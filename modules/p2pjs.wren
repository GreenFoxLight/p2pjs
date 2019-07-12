foreign class Job {
    construct launch(path, arg) {} 

    foreign isFinished

    foreign result 
    
    foreign arg

    toString {
        if (isFinished) {
            return arg.toString + ": " + result.toString
        } else {
            return arg.toString + ": " + "<Still running>"
        }
    }
}

class Interface {
    foreign static numberOfOutstandingJobs

    foreign static idle()
}
