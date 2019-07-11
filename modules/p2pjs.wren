foreign class Job {
    construct launch(path, arg) {} 

    foreign isFinished()

    foreign getResult()
    
    foreign getArgument()

    toString {
        if (isFinished()) {
            return getArgument().toString + ": " + getResult().toString
        } else {
            return getArgument().toString + ": " + "<Still running>"
        }
    }
}

class Interface {
    foreign static getNumberOfOutstandingJobs()

    foreign static idle()
}
