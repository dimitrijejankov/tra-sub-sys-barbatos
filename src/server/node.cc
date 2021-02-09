#include <memory>
#include <sstream>
#include "node.h"
#include "../commands/command_compiler.h"

void bbts::node_t::init() {

  // the communicator
  _comm = std::make_shared<communicator_t>(_config);

  // the logger
  _logger = std::make_shared<logger_t>(_config);

  // create the tensor stats
  _stats = std::make_shared<tensor_stats_t>();

  // create the storage
  _storage = std::make_shared<storage_t>();

  // init the factory
  _factory = std::make_shared<bbts::tensor_factory_t>();

  // init the udf manager
  _udf_manager = std::make_shared<bbts::udf_manager_t>(_factory);

  // init the reservation station
  _res_station = std::make_shared<bbts::reservation_station_t>(_comm->get_rank(),
                                                               _comm->get_num_nodes());

  // this runs commands
  _command_runner = std::make_shared<bbts::command_runner_t>(_storage, _factory, _udf_manager,
                                                             _res_station, _comm, _logger, _stats);

  // the tensor notifier
  _tensor_notifier = std::make_shared<bbts::tensor_notifier_t>(_comm, _res_station);

  // the scheduler
  _coordinator = std::make_shared<coordinator_t>(_comm, _res_station, _logger,
                                                 _storage, _command_runner, _tensor_notifier, _factory,  _stats);
}

void bbts::node_t::run() {

  /// 1.0 Kick off all the stuff that needs to run

  // this will delete the tensors
  auto deleter = create_deleter_thread();

  // the command processing threads
  std::vector<std::thread> command_processing_threads;
  command_processing_threads.reserve(_comm->get_num_nodes());
  for (node_id_t t = 0; t < _config->num_threads; ++t) {
    command_processing_threads.push_back(std::move(create_command_processing_thread()));
  }

  // this will get all the notifications about tensors
  auto tsn_thread = tensor_notifier();

  // this kicks off and handles remove commands (MOVE and REDUCE)
  auto command_expect = expect_remote_command();

  // if this is the root node, it does not have a coordinator thread
  std::thread coord_thread;
  if(get_rank() != 0) {

    // this will accept requests from the coordinator
    coord_thread = create_coordinator_thread();
  }

  // notification sender
  std::vector<std::thread> remote_notification_sender;
  remote_notification_sender.reserve(_config->num_threads);
  for(node_id_t node = 0; node < _comm->get_num_nodes(); ++node) {

    // no need to notify self so skip that
    if(node == _comm->get_rank()) {
      continue;
    }

    // create the notifier thread
    remote_notification_sender.push_back(remote_tensor_notification_sender(node));
  }

  /// 2.0 Wait for stuff to finish

  // wait for the notification sender threads to finish
  for(auto &rns : remote_notification_sender) {
    rns.join();
  }

  // wait for the remote command handler threads to finish
  command_expect.join();

  // wait for the notifier to finish
  tsn_thread.join();

  // wait for the command processing thread to finish
  for(auto &cpt : command_processing_threads) {
    cpt.join();
  }

  // wait for the deleter to shutdown
  deleter.join();

  // we only have a coordinator thread if we are not the root node
  if(get_rank() != 0) {
    coord_thread.join();
  }
}

size_t bbts::node_t::get_num_nodes() const {
  return _comm->get_num_nodes();
}

size_t bbts::node_t::get_rank() const {
  return _comm->get_rank();
}


void bbts::node_t::print_cluster_info(std::ostream& out) {

  out << "Cluster Information : \n";
  out << "\tNumber of Nodes : " << _comm->get_num_nodes() << " \n";
  out << "\tNumber of physical cores : " << _config->num_threads << " \n";
  out << "\tTotal RAM : " << _config->total_ram / (1024 * 1024) << " MB \n";
}


std::tuple<bool, std::string> bbts::node_t::load_commands(const std::vector<command_ptr_t> &cmds) {

  // schedule all commands
  return _coordinator->schedule_commands(cmds);
}

std::tuple<bool, std::string> bbts::node_t::load_commands(const bbts::parsed_command_list_t &cmds) {

  // log the loaded commands
  std::ostringstream ss; cmds.print(ss);
  _logger->message(ss.str());

  // init the compiler
  command_compiler_t compiler(*_factory, *_udf_manager);

  // compile the commands
  try {

    // the compiled commands
    auto compiled_cmds = compiler.compile(cmds);

    // schedule all commands
    return _coordinator->schedule_commands(compiled_cmds);
  }
  catch (const std::runtime_error& ex) {
    return {false, ex.what()};
  }
}

std::tuple<bool, std::string> bbts::node_t::run_commands() {

  // run all the commands
  return _coordinator->run_commands();
}

std::tuple<bool, std::string> bbts::node_t::set_verbose(bool val) {
  return _coordinator->set_verbose(val);
}

std::tuple<bool, std::string> bbts::node_t::print_storage_info() {
  return _coordinator->print_storage_info();
}

std::tuple<bool, std::string> bbts::node_t::print_tensor_info(tid_t id) {
  return _coordinator->print_tensor_info(id);
}

std::tuple<bool, std::string> bbts::node_t::clear() {
  return _coordinator->clear();
}

std::tuple<bool, std::string> bbts::node_t::shutdown_cluster() {
  return _coordinator->shutdown_cluster();
}

void bbts::node_t::sync() {

  _comm->barrier();
}

std::thread bbts::node_t::create_deleter_thread() {

  // create the thread
  return std::thread([this]() {

    _command_runner->run_deleter();
  });
}
std::thread bbts::node_t::create_command_processing_thread() {

  // create the thread to pull
  std::thread t = std::thread([this]() {

    _command_runner->local_command_runner();
  });

  return std::move(t);
}
std::thread bbts::node_t::expect_remote_command() {

  // create the thread
  std::thread t = std::thread([this]() {

    _command_runner->remote_command_handler();
  });

  return std::move(t);
}
std::thread bbts::node_t::remote_tensor_notification_sender(bbts::node_id_t out_node) {

  // create the thread
  std::thread t = std::thread([out_node, this]() {

    // this will send notifications to out node
    _tensor_notifier->run_notification_sender_for_node(out_node);
  });

  return std::move(t);
}

std::thread bbts::node_t::create_coordinator_thread() {

  // create the thread
  std::thread t = std::thread([this]() {

    // this will send notifications to out node
    _coordinator->accept();
  });

  return std::move(t);
}

std::thread bbts::node_t::tensor_notifier() {

  // create the thread
  std::thread t = std::thread([this]() {

    // run the handler for the notifications
    _tensor_notifier->run_notification_handler();
  });

  return std::move(t);
}



