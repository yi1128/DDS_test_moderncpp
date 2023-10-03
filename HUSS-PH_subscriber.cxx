/*
* (c) Copyright, Real-Time Innovations, 2020.  All rights reserved.
* RTI grants Licensee a license to use, modify, compile, and create derivative
* works of the software solely for use with RTI Connext DDS. Licensee may
* redistribute copies of the software provided that all such copies are subject
* to this license. The software is provided "as is", with no warranty of any
* type, including any warranty for fitness for any purpose. RTI is under no
* obligation to maintain or support the software. RTI shall not be liable for
* any incidental or consequential damages arising out of the use or inability
* to use the software.
*/

#include <algorithm>
#include <iostream>

#include <dds/sub/ddssub.hpp>
#include <dds/core/ddscore.hpp>
#include <rti/core/cond/AsyncWaitSet.hpp>
#include <rti/util/util.hpp>  // for sleep()
#include <rti/config/Logger.hpp>  // for logging
// alternatively, to include all the standard APIs:
//  <dds/dds.hpp>
// or to include both the standard APIs and extensions:
//  <rti/rti.hpp>
//
// For more information about the headers and namespaces, see:
//    https://community.rti.com/static/documentation/connext-dds/7.0.0/doc/api/connext_dds/api_cpp2/group__DDSNamespaceModule.html
// For information on how to use extensions, see:
//    https://community.rti.com/static/documentation/connext-dds/7.0.0/doc/api/connext_dds/api_cpp2/group__DDSCpp2Conventions.html

#include "HUSS-PH.hpp"
#include "application.hpp"  // for command line parsing and ctrl-c

class AwsSubscriber {
public:
    static const std::string TOPIC_NAME;

    AwsSubscriber(
        DDS_DomainId_t domain_id,
        rti::core::cond::AsyncWaitSet async_waitset);

    void process_received_samples();

    int received_count();

    ~AwsSubscriber();

public:
    // Entities to receive data
    dds::sub::DataReader<CTRMGR_LAS_DETECTION_DATA> receiver_;
    // Reference to the AWS used for processing the events
    rti::core::cond::AsyncWaitSet async_waitset_;
};

class DataAvailableHandler {
public:
    /* Handles the reception of samples */
    void operator()()
    {
        subscriber_.process_received_samples();
    }

    DataAvailableHandler(AwsSubscriber& subscriber) : subscriber_(subscriber)
    {
    }

private:
    AwsSubscriber& subscriber_;
};

// AwsSubscriberPlayer implementation

const std::string AwsSubscriber::TOPIC_NAME = "CTRMGR_LAS_DETECTION_DATA";

AwsSubscriber::AwsSubscriber(
    DDS_DomainId_t domain_id,
    rti::core::cond::AsyncWaitSet async_waitset)
    : receiver_(dds::core::null), async_waitset_(async_waitset)
{
    // Create a DomainParticipant with default Qos
    dds::core::QosProvider qos_provider = dds::core::QosProvider::Default();
    dds::domain::DomainParticipant participant(domain_id, qos_provider.participant_qos("hussqos::ParticipantQos"));

    // Create a Topic -- and automatically register the type
    dds::topic::Topic<CTRMGR_LAS_DETECTION_DATA> topic(participant, "CTRMGR_LAS_DETECTION_DATA");

    // Create a DataReader with default Qos (Subscriber created in-line)

    dds::sub::qos::DataReaderQos reader_qos =
        qos_provider.datareader_qos("hussqosbytopic::CTRMGR_LAS_DETECTION_DATA");

    receiver_ = dds::sub::DataReader<CTRMGR_LAS_DETECTION_DATA>(
        dds::sub::Subscriber(participant, qos_provider.subscriber_qos()),
        topic,
        reader_qos);

    // DataReader status condition: to process the reception of samples
    dds::core::cond::StatusCondition reader_status_condition(receiver_);
    reader_status_condition.enabled_statuses(
        dds::core::status::StatusMask::data_available());
    reader_status_condition->handler(DataAvailableHandler(*this));
    async_waitset_.attach_condition(reader_status_condition);
}

void AwsSubscriber::process_received_samples()
{
    // Take all samples This will reset the StatusCondition
    dds::sub::LoanedSamples<CTRMGR_LAS_DETECTION_DATA> samples = receiver_.take();

    // Release status condition in case other threads can process outstanding
    // samples
    async_waitset_.unlock_condition(
        dds::core::cond::StatusCondition(receiver_));

    // Process sample
    for (const auto& sample : samples) {
        if (sample.info().valid()) {
            //std::cout << "Received sample:\n\t" << sample.data() << std::endl;
            std::cout << "Received data" << sample.data().stMsgHeader().lSequenceNo() << std::endl;
        }
    }
    // Sleep a random amount of time between 1 and 10 secs. This is
    // intended to cause the handling thread of the AWS to take a long
    // time dispatching
    rti::util::sleep(dds::core::Duration::from_secs(rand() % 10 + 1));
}

int AwsSubscriber::received_count()
{
    return receiver_->datareader_protocol_status()
        .received_sample_count()
        .total();
}

AwsSubscriber::~AwsSubscriber()
{
    async_waitset_.detach_condition(
        dds::core::cond::StatusCondition(receiver_));
}


int main(int argc, char* argv[])
{
    using namespace application;

    srand(time(NULL));
    /*
    // Parse arguments and handle control-C
    auto arguments = parse_arguments(argc, argv, ApplicationKind::Subscriber);
    if (arguments.parse_result == ParseReturn::exit) {
        return EXIT_SUCCESS;
    }
    else if (arguments.parse_result == ParseReturn::failure) {
        return EXIT_FAILURE;
    }
    setup_signal_handlers();

    // Sets Connext verbosity to help debugging
    rti::config::Logger::instance().verbosity(arguments.verbosity);
    */
    try {
        // An AsyncWaitSet (AWS) for multi-threaded events .
        // The AWS will provide the infrastructure to receive samples using
        // multiple threads.
        rti::core::cond::AsyncWaitSet async_waitset(
            rti::core::cond::AsyncWaitSetProperty().thread_pool_size(
                4));

        async_waitset.start();

        AwsSubscriber subscriber(1, async_waitset);

        std::cout << "Wait for samples..." << std::endl;
        while (!application::shutdown_requested
            && subscriber.received_count() < 1000) {
            rti::util::sleep(dds::core::Duration(1));
        }

    }
    catch (const std::exception& ex) {
        // This will catch DDS exceptions
        std::cerr << "Exception in main(): " << ex.what() << std::endl;
        return -1;
    }

    // Releases the memory used by the participant factory.  Optional at
    // application exit
    dds::domain::DomainParticipant::finalize_participant_factory();

    return EXIT_SUCCESS;
}
