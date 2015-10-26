/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2015 SciDB, Inc.
* All Rights Reserved.
*
* SciDB is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

/**
 * @file Query.cpp
 *
 * @author roman.simakov@gmail.com
 *
 * @brief Implementation of query context methods
 */
#include <malloc.h>
#include <time.h>
#include <memory>
#include <boost/foreach.hpp>
#include <boost/serialization/string.hpp>
#include <boost/archive/text_iarchive.hpp>

#include <query/QueryProcessor.h>
#include <query/RemoteArray.h>
#include "array/DBArray.h"
#include <network/MessageUtils.h>
#include <network/NetworkManager.h>
#include <network/MessageHandleJob.h>
#include <system/BlockCyclic.h>
#include <system/Cluster.h>
#include <system/Exceptions.h>
#include <system/SciDBConfigOptions.h>
#include <system/SystemCatalog.h>
#include <system/System.h>
#include <smgr/io/ReplicationManager.h>
#include <smgr/io/Storage.h>
#include <util/LockManager.h>
#include <util/iqsort.h>
#include <util/session/Session.h>
#ifndef SCIDB_CLIENT
#include <util/MallocStats.h>
#endif


namespace scidb
{
using namespace std;
using namespace arena;
using namespace boost;

// Query class implementation
Mutex Query::queriesMutex;
Query::Queries Query::_queries;
uint32_t Query::nextID = 0;
log4cxx::LoggerPtr Query::_logger = log4cxx::Logger::getLogger("scidb.qproc.processor");
log4cxx::LoggerPtr UpdateErrorHandler::_logger = log4cxx::Logger::getLogger("scidb.qproc.processor");
log4cxx::LoggerPtr RemoveErrorHandler::_logger = log4cxx::Logger::getLogger("scidb.qproc.processor");
log4cxx::LoggerPtr BroadcastAbortErrorHandler::_logger = log4cxx::Logger::getLogger("scidb.qproc.processor");
boost::mt19937 Query::_rng;

size_t Query::PendingRequests::increment()
{
    ScopedMutexLock cs(_mutex);
    _nReqs += 1;
    return _nReqs;
}

bool Query::PendingRequests::decrement()
{
    ScopedMutexLock cs(_mutex);
    if (--_nReqs == 0 && _sync) {
        _sync = false;
        return true;
    }
    return false;
}

bool Query::PendingRequests::test()
{
    ScopedMutexLock cs(_mutex);
    if (_nReqs != 0) {
        _sync = true;
        return false;
    }
    return true;
}

std::shared_ptr<Query> Query::createDetached(QueryID queryID)
{
    std::shared_ptr<Query> query = std::make_shared<Query>(queryID);

    const size_t smType = Config::getInstance()->getOption<int>(CONFIG_STAT_MONITOR);
    if (smType) {
        const string& smParams = Config::getInstance()->getOption<string>(CONFIG_STAT_MONITOR_PARAMS);
        query->statisticsMonitor = StatisticsMonitor::create(smType, smParams);
    }

    return query;
}

std::shared_ptr<Query> Query::createFakeQuery(InstanceID coordID,
                                                InstanceID localInstanceID,
                                                std::shared_ptr<const InstanceLiveness> liveness,
                                                int32_t *longErrorCode)
{
  std::shared_ptr<Query> query = createDetached(FAKE_QUERY_ID);
  try {

      query->init(coordID, localInstanceID, liveness);

  } catch (const scidb::Exception& e) {
      if (longErrorCode != NULL) {
          *longErrorCode = e.getLongErrorCode();
      } else {
          destroyFakeQuery(query.get());
          throw;
      }
  } catch (const std::exception& e) {
      destroyFakeQuery(query.get());
      throw;
  }
  return query;
}

void Query::destroyFakeQuery(Query* q)
 {
     if (q!=NULL && q->getQueryID() == 0) {
         try {
             q->handleAbort();
         } catch (scidb::Exception&) { }
     }
 }


Query::Query(QueryID queryID):
    _queryID(queryID),
    _instanceID(INVALID_INSTANCE),
    _coordinatorID(INVALID_INSTANCE),
    _error(SYSTEM_EXCEPTION_SPTR(SCIDB_E_NO_ERROR, SCIDB_E_NO_ERROR)),
    _completionStatus(INIT),
    _commitState(UNKNOWN),
    _creationTime(time(NULL)),
    _useCounter(0),
    _doesExclusiveArrayAccess(false),
    _procGrid(NULL), isDDL(false)
{
}

Query::~Query()
{
    LOG4CXX_TRACE(_logger, "Query::~Query() " << _queryID << " "<<(void*)this);
    LOG4CXX_DEBUG(_logger, "Query._arena:" << *_arena);
    if (statisticsMonitor) {
        statisticsMonitor->pushStatistics(*this);
    }
    delete _procGrid ; _procGrid = NULL ;
}

void Query::init(InstanceID coordID,
                 InstanceID localInstanceID,
                 std::shared_ptr<const InstanceLiveness>& liveness)
{
   assert(liveness);
   assert(localInstanceID != INVALID_INSTANCE);
   {
      ScopedMutexLock cs(errorMutex);

      validate();

      assert( _queryID != INVALID_QUERY_ID);

   /* Install a special arena within the query that all local operator arenas
      should delagate to; we're now using a LeaArena here -  an adaptation of
      Doug Lea's design with a tunable set of bin sizes - because it not only
      supports recycling but also suballocates all of the blocks it hands out
      from large - currently 64 MiB - slabs that are given back to the system
      en masse no later than when the query completes;  the hope here is that
      this reduces the overall fragmentation of the system heap...*/
      {
          assert(_arena == 0);
          char s[64];
          snprintf(s,SCIDB_SIZE(s),"query %lu",_queryID);

          _arena = newArena(Options(s).lea(arena::getArena(),64*MiB));
      }

      assert(!_coordinatorLiveness);
      _coordinatorLiveness = liveness;
      assert(_coordinatorLiveness);

      size_t nInstances = _coordinatorLiveness->getNumLive();
      if (nInstances <= 0) {
          throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_LIVENESS_EMPTY);
      }
      assert(_liveInstances.size() == 0);
      _liveInstances.clear();
      _liveInstances.reserve(nInstances);

      const InstanceLiveness::LiveInstances& liveInstances = _coordinatorLiveness->getLiveInstances();
      assert(liveInstances.size() == nInstances);
      for ( InstanceLiveness::LiveInstances::const_iterator iter = liveInstances.begin();
        iter != liveInstances.end(); ++iter) {
         _liveInstances.push_back((*iter)->getInstanceId());
      }
      _instanceID = mapPhysicalToLogical(localInstanceID);
      assert(_instanceID != INVALID_INSTANCE);
      assert(_instanceID < nInstances);

      if (coordID == INVALID_INSTANCE) {
         _coordinatorID = INVALID_INSTANCE;
         std::shared_ptr<Query::ErrorHandler> ptr(new BroadcastAbortErrorHandler());
         pushErrorHandler(ptr);
      } else {
         _coordinatorID = mapPhysicalToLogical(coordID);
         assert(_coordinatorID < nInstances);
      }

      _receiveSemaphores.resize(nInstances);
      _receiveMessages.resize(nInstances);
      chunkReqs.resize(nInstances);
      Finalizer f = bind(&Query::destroyFinalizer, _1);
      pushFinalizer(f);
      _errorQueue = NetworkManager::getInstance()->createWorkQueue();
      assert(_errorQueue);
      _errorQueue->start();
      _bufferReceiveQueue = NetworkManager::getInstance()->createWorkQueue();
      assert(_bufferReceiveQueue);
      _bufferReceiveQueue->start();
      _operatorQueue = NetworkManager::getInstance()->createWorkQueue();
      _operatorQueue->stop();
      assert(_operatorQueue);
      _replicationCtx = std::make_shared<ReplicationContext>(shared_from_this(), nInstances);
      assert(_replicationCtx);
   }

   // register for notifications
   InstanceLivenessNotification::PublishListener listener =
      boost::bind(&Query::handleLivenessNotification, shared_from_this(), _1);
   _livenessListenerID = InstanceLivenessNotification::addPublishListener(listener);

   LOG4CXX_DEBUG(_logger, "Initialized query (" << _queryID << ")");
}

std::shared_ptr<Query> Query::insert(const std::shared_ptr<Query>& query)
{
    assert(query);
    assert(query->getQueryID()>0);

   // queriesMutex must be locked
   pair<Queries::iterator,bool> res =
   _queries.insert( std::make_pair ( query->getQueryID(), query ) );
   setCurrentQueryID(query->getQueryID());

   if (res.second) {
       const uint32_t nRequests = std::max(Config::getInstance()->getOption<int>(CONFIG_REQUESTS),1);
       if (_queries.size() > nRequests) {
           _queries.erase(res.first);
           throw (SYSTEM_EXCEPTION(SCIDB_SE_NO_MEMORY, SCIDB_LE_RESOURCE_BUSY) << "too many queries");
       }
       assert(res.first->second == query);
       LOG4CXX_DEBUG(_logger, "Allocating query (" << query->getQueryID() << ")");
       LOG4CXX_DEBUG(_logger, "Number of allocated queries = " << _queries.size());
       return query;
   }
   return res.first->second;
}

QueryID Query::generateID()
{
   const uint64_t instanceID = StorageManager::getInstance().getInstanceId();

   ScopedMutexLock mutexLock(queriesMutex);

   const uint32_t timeVal = time(NULL);
   const uint32_t clockVal = clock();

   // It allows us to have about 16 000 000 instances while about 10 000 years
   QueryID queryID = ((instanceID+1) << 40) | (timeVal + clockVal + nextID++);
   LOG4CXX_DEBUG(_logger, "Generated queryID: instanceID=" << instanceID << ", time=" << timeVal
                 << ", clock=" << clockVal<< ", nextID=" << nextID - 1
                 << ", queryID=" << queryID);
   return queryID;
}

std::shared_ptr<Query> Query::create(QueryID queryID, InstanceID instanceId)
{
    assert(queryID > 0 && queryID != INVALID_QUERY_ID);
    std::shared_ptr<Query> query = createDetached(queryID);
    assert(query);
    assert(query->_queryID == queryID);

    std::shared_ptr<const scidb::InstanceLiveness> myLiveness =
       Cluster::getInstance()->getInstanceLiveness();
    assert(myLiveness);

    query->init(instanceId,
                Cluster::getInstance()->getLocalInstanceId(),
                myLiveness);
    {
       ScopedMutexLock mutexLock(queriesMutex);

       if (insert(query) != query) {
           throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_DUPLICATE_QUERY_ID);
       }
    }
    return query;
}

void Query::start()
{
    ScopedMutexLock cs(errorMutex);
    checkNoError();
    if (_completionStatus == INIT) {
        _completionStatus = START;
    }
}

void Query::stop()
{
    ScopedMutexLock cs(errorMutex);
    checkNoError();
    if (_completionStatus == START) {
        _completionStatus = INIT;
    }
}

void Query::pushErrorHandler(const std::shared_ptr<ErrorHandler>& eh)
{
    assert(eh);
    ScopedMutexLock cs(errorMutex);
    checkNoError();
    _errorHandlers.push_back(eh);
}

void Query::pushFinalizer(const Finalizer& f)
{
    assert(f);
    ScopedMutexLock cs(errorMutex);
    checkNoError();
    _finalizers.push_back(f);
}

void Query::done()
{
    ScopedMutexLock cs(errorMutex);
    if (SCIDB_E_NO_ERROR != _error->getLongErrorCode())
    {
        _completionStatus = ERROR;
        _error->raise();
    }
    _completionStatus = OK;
}

void Query::done(const std::shared_ptr<Exception> unwindException)
{
    bool isAbort = false;
    std::shared_ptr<const scidb::Exception> msg;
    {
        ScopedMutexLock cs(errorMutex);
        if (SCIDB_E_NO_ERROR == _error->getLongErrorCode())
        {
            _error = unwindException;
            _error->setQueryId(_queryID);
            msg = _error;
        }
        _completionStatus = ERROR;
        isAbort = (_commitState != UNKNOWN);

        LOG4CXX_DEBUG(_logger, "Query::done: queryID=" << _queryID
                      << ", _commitState=" << _commitState
                      << ", errorCode=" << _error->getLongErrorCode());
    }
    if (msg) {
        Notification<scidb::Exception> event(msg);
        event.publish();
    }
    if (isAbort) {
        handleAbort();
    }
}

bool Query::doesExclusiveArrayAccess()
{
    return _doesExclusiveArrayAccess;
}

std::shared_ptr<SystemCatalog::LockDesc>
Query::requestLock(std::shared_ptr<SystemCatalog::LockDesc>& requestedLock)
{
    assert(requestedLock);
    assert(!requestedLock->isLocked());
    ScopedMutexLock cs(errorMutex);

    if (requestedLock->getLockMode() > SystemCatalog::LockDesc::RD) {
        _doesExclusiveArrayAccess = true;
    }

    pair<SystemCatalog::QueryLocks::const_iterator, bool> res = _requestedLocks.insert(requestedLock);
    if (res.second) {
        assert((*res.first).get() == requestedLock.get());
        LOG4CXX_DEBUG(_logger, "Requested lock: " << (*res.first)->toString() << " inserted");
        return requestedLock;
    }

    if ((*(res.first))->getLockMode() < requestedLock->getLockMode()) {
        _requestedLocks.erase(res.first);
        res = _requestedLocks.insert(requestedLock);
        assert(res.second);
        assert((*res.first).get() == requestedLock.get());
        LOG4CXX_DEBUG(_logger, "Promoted lock: " << (*res.first)->toString() << " inserted");
    }
    return (*(res.first));
}

void Query::handleError(const std::shared_ptr<Exception>& unwindException)
{
    assert(unwindException);
    assert(unwindException->getLongErrorCode() != SCIDB_E_NO_ERROR);
    std::shared_ptr<const scidb::Exception> msg;
    {
        ScopedMutexLock cs(errorMutex);

        if (_error->getLongErrorCode() == SCIDB_E_NO_ERROR)
        {
            _error = unwindException;
            _error->setQueryId(_queryID);
            msg = _error;
        }
    }
    if (msg) {
        Notification<scidb::Exception> event(msg);
        event.publish();
    }
}

bool Query::checkFinalState()
{
   ScopedMutexLock cs(errorMutex);
   return ( _finalizers.empty() &&
            ((_completionStatus == INIT &&
              _error->getLongErrorCode() != SCIDB_E_NO_ERROR) ||
             _completionStatus == OK ||
             _completionStatus == ERROR) );
}

void Query::invokeFinalizers(deque<Finalizer>& finalizers)
{
   assert(finalizers.empty() || checkFinalState());
   for (deque<Finalizer>::reverse_iterator riter = finalizers.rbegin();
        riter != finalizers.rend(); riter++) {
      Finalizer& fin = *riter;
      if (!fin) {
         continue;
      }
      try {
         fin(shared_from_this());
      } catch (const std::exception& e) {
         LOG4CXX_FATAL(_logger, "Query (" << _queryID
                       << ") finalizer failed:"
                       << e.what()
                       << "Aborting!");
         abort();
      }
   }
}

void Query::invokeErrorHandlers(deque<std::shared_ptr<ErrorHandler> >& errorHandlers)
{
    for (deque<std::shared_ptr<ErrorHandler> >::reverse_iterator riter = errorHandlers.rbegin();
         riter != errorHandlers.rend(); riter++) {
        std::shared_ptr<ErrorHandler>& eh = *riter;
        try {
            eh->handleError(shared_from_this());
        } catch (const std::exception& e) {
            LOG4CXX_FATAL(_logger, "Query (" << _queryID
                          << ") error handler failed:"
                          << e.what()
                          << "Aborting!");
            abort();
        }
    }
}

void Query::handleAbort()
{
    QueryID queryId = INVALID_QUERY_ID;
    deque<Finalizer> finalizersOnStack;
    deque<std::shared_ptr<ErrorHandler> > errorHandlersOnStack;
    std::shared_ptr<const scidb::Exception> msg;
    {
        ScopedMutexLock cs(errorMutex);

        queryId = _queryID;
        LOG4CXX_DEBUG(_logger, "Query (" << queryId << ") is being aborted");

        if(_commitState == COMMITTED) {
            LOG4CXX_ERROR(_logger, "Query (" << queryId
                          << ") cannot be aborted after commit."
                          << " completion status=" << _completionStatus
                          << " commit status=" << _commitState
                          << " error=" << _error->getLongErrorCode());
            assert(false);
            throw (SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_INVALID_COMMIT_STATE) << _queryID << "abort");
        }

        _commitState = ABORTED;

        if (_error->getLongErrorCode() == SCIDB_E_NO_ERROR)
        {
            _error = (SYSTEM_EXCEPTION_SPTR(SCIDB_SE_QPROC, SCIDB_LE_QUERY_CANCELLED) << queryId);
            _error->setQueryId(queryId);
            msg = _error;
        }
        if (_completionStatus == START)
        {
            LOG4CXX_DEBUG(_logger, "Query (" << queryId << ") is still in progress");
            return;
        }
        errorHandlersOnStack.swap(_errorHandlers);
        finalizersOnStack.swap(_finalizers);
    }
    if (msg) {
        Notification<scidb::Exception> event(msg);
        event.publish();
    }
    if (!errorHandlersOnStack.empty()) {
        LOG4CXX_ERROR(_logger, "Query (" << queryId << ") error handlers ("
                     << errorHandlersOnStack.size() << ") are being executed");
        invokeErrorHandlers(errorHandlersOnStack);
        errorHandlersOnStack.clear();
    }
    freeQuery(queryId);
    invokeFinalizers(finalizersOnStack);
}

void Query::handleCommit()
{
    QueryID queryId = INVALID_QUERY_ID;
    deque<Finalizer> finalizersOnStack;
    std::shared_ptr<const scidb::Exception> msg;
    {
        ScopedMutexLock cs(errorMutex);

        queryId = _queryID;

        LOG4CXX_DEBUG(_logger, "Query (" << _queryID << ") is being committed");

        if (_completionStatus != OK || _commitState == ABORTED) {
            LOG4CXX_ERROR(_logger, "Query (" << _queryID
                          << ") cannot be committed after abort."
                          << " completion status=" << _completionStatus
                          << " commit status=" << _commitState
                          << " error=" << _error->getLongErrorCode());
            assert(false);
            throw (SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_INVALID_COMMIT_STATE) << _queryID << "commit");
        }

        _errorHandlers.clear();

        _commitState = COMMITTED;

        if (_error->getLongErrorCode() == SCIDB_E_NO_ERROR)
        {
            _error = SYSTEM_EXCEPTION_SPTR(SCIDB_SE_QPROC, SCIDB_LE_QUERY_ALREADY_COMMITED);
            (*static_cast<scidb::SystemException*>(_error.get())) << queryId;
            _error->setQueryId(queryId);
            msg = _error;
        }
        finalizersOnStack.swap(_finalizers);
    }
    if (msg) {
        Notification<scidb::Exception> event(msg);
        event.publish();
    }
    assert(queryId != INVALID_QUERY_ID);
    freeQuery(queryId);
    invokeFinalizers(finalizersOnStack);
}

void Query::handleComplete()
{
    handleCommit();
    std::shared_ptr<MessageDesc>  msg(makeCommitMessage(_queryID));
    NetworkManager::getInstance()->broadcastPhysical(msg);
}

void Query::handleCancel()
{
    handleAbort();
}

void Query::handleLivenessNotification(std::shared_ptr<const InstanceLiveness>& newLiveness)
{
    QueryID thisQueryId(0);
    InstanceID coordPhysId = INVALID_INSTANCE;
    std::shared_ptr<const scidb::Exception> msg;
    bool isAbort = false;
    {
        ScopedMutexLock cs(errorMutex);

        assert(newLiveness->getVersion() >= _coordinatorLiveness->getVersion());

        if (newLiveness->getVersion() == _coordinatorLiveness->getVersion()) {
            assert(newLiveness->isEqual(*_coordinatorLiveness));
            return;
        }

        LOG4CXX_ERROR(_logger, "Query " << _queryID << " is aborted on changed liveness");

        if (_error->getLongErrorCode() == SCIDB_E_NO_ERROR)
        {
            _error = SYSTEM_EXCEPTION_SPTR(SCIDB_SE_QPROC, SCIDB_LE_NO_QUORUM);
            _error->setQueryId(_queryID);
            msg = _error;
        }

        if (_coordinatorID != INVALID_INSTANCE) {
            coordPhysId = getPhysicalCoordinatorID();

            InstanceLiveness::InstancePtr newCoordState = newLiveness->find(coordPhysId);
            isAbort = newCoordState->isDead();
            if (!isAbort) {
                InstanceLiveness::InstancePtr oldCoordState = _coordinatorLiveness->find(coordPhysId);
                isAbort = (newCoordState != oldCoordState);
            }
        }
        // If the coordinator is dead, we abort the query.
        // There is still a posibility that the coordinator actually has committed.
        // For read queries it does not matter.
        // For write queries UpdateErrorHandler::handleErrorOnWorker() will wait (while holding its own array lock)
        // until the coordinator array lock is released and decide whether to really abort
        // based on the state of the catalog (i.e. if the new version is recorded).

        if (!_errorQueue) {
            LOG4CXX_TRACE(_logger, "Liveness change will not be handled for a deallocated query (" << _queryID << ")");
            isAbort = false;
        }
        thisQueryId = _queryID;
    }
    if (msg) {
        Notification<scidb::Exception> event(msg);
        event.publish();
    }
    if (!isAbort) {
        return;
    }
    try {
        std::shared_ptr<MessageDesc> msg = makeAbortMessage(thisQueryId);

        // HACK (somewhat): set sourceid to coordinator, because only it can issue an abort
        assert(coordPhysId != INVALID_INSTANCE);
        msg->setSourceInstanceID(coordPhysId);

        std::shared_ptr<MessageHandleJob> job = make_shared<ServerMessageHandleJob>(msg);
        std::shared_ptr<WorkQueue> rq = NetworkManager::getInstance()->getRequestQueue();
        std::shared_ptr<WorkQueue> wq = NetworkManager::getInstance()->getWorkQueue();
        job->dispatch(rq, wq);

    } catch (const scidb::Exception& e) {
        LOG4CXX_ERROR(_logger, "Failed to abort queryID=" << thisQueryId
                      << " on coordinator liveness change because: " << e.what());
        throw;
    }
}

InstanceID Query::getPhysicalCoordinatorID(bool resolveLocalInstanceId)
{
    InstanceID coord = _coordinatorID;
    if (_coordinatorID == INVALID_INSTANCE) {
        if (!resolveLocalInstanceId) {
            return INVALID_INSTANCE;
        }
        coord = _instanceID;
    }
    assert(_liveInstances.size() > 0);
    assert(_liveInstances.size() > coord);
    return _liveInstances[coord];
}

InstanceID Query::mapLogicalToPhysical(InstanceID instance)
{
   if (instance == INVALID_INSTANCE) {
      return instance;
   }
   ScopedMutexLock cs(errorMutex);
   assert(_liveInstances.size() > 0);
   if (instance >= _liveInstances.size()) {
       throw SYSTEM_EXCEPTION(SCIDB_SE_QPROC, SCIDB_LE_INSTANCE_OFFLINE) << instance;
   }
   checkNoError();
   instance = _liveInstances[instance];
   return instance;
}

InstanceID Query::mapPhysicalToLogical(InstanceID instanceID)
{
   ScopedMutexLock cs(errorMutex);
   assert(_liveInstances.size() > 0);
   size_t index=0;
   bool found = bsearch(_liveInstances, instanceID, index);
   if (!found) {
       throw SYSTEM_EXCEPTION(SCIDB_SE_QPROC, SCIDB_LE_INSTANCE_OFFLINE) << instanceID;
   }
   return index;
}

bool Query::isPhysicalInstanceDead(InstanceID instance)
{
   ScopedMutexLock cs(errorMutex);
   checkNoError();
   bool isDead = _coordinatorLiveness->isDead(instance);
   assert(isDead ||
          _coordinatorLiveness->find(instance));
   return isDead;
}


bool Query::isDistributionDegraded(const ArrayDesc& desc)
{
    // For now, all arrays are distributed to all the instances
    // (and that instance set never changes).
    // In the future, arrays will be allowed to exist on different instances.

    const size_t redundancy = Config::getInstance()->getOption<size_t>(CONFIG_REDUNDANCY);
    Cluster* cluster = Cluster::getInstance();
    SCIDB_ASSERT(cluster);
    std::shared_ptr<const InstanceMembership> membership(cluster->getInstanceMembership());
    SCIDB_ASSERT(membership);
    ASSERT_EXCEPTION( (membership->getViewId() == getCoordinatorLiveness()->getViewId()),
                      "Cluster membership cannot change (yet)");
    ASSERT_EXCEPTION( (getInstancesCount() <= membership->getInstances().size()),
                      "Invalid membership and/or liveness");
    ASSERT_EXCEPTION( ((getInstancesCount() + redundancy) >= membership->getInstances().size()),
                      "No read quorum"); // otherwise we should not be executing this query
    if (getInstancesCount() == membership->getInstances().size()) {
        return false; // everyone is alive
    }
    return true;
}

static __thread QueryID currentQueryID = 0;
QueryID Query::getCurrentQueryID()
{
    return currentQueryID;
}

void Query::setCurrentQueryID(QueryID queryID)
{
    currentQueryID = queryID;
}

std::shared_ptr<Query> Query::getQueryByID(QueryID queryID, bool raise)
{
    std::shared_ptr<Query> query;
    ScopedMutexLock mutexLock(queriesMutex);

    Queries::const_iterator q = _queries.find(queryID);
    if (q != _queries.end()) {
        setCurrentQueryID(queryID);
        return q->second;
    }
    LOG4CXX_DEBUG(_logger, "Query " << queryID << " is not found");
    if (raise) {
        throw SYSTEM_EXCEPTION(SCIDB_SE_QPROC, SCIDB_LE_QUERY_NOT_FOUND) << queryID;
    }
    assert(!query);
    return query;
}

void Query::freeQueries()
{
    Queries queries;
    {
        ScopedMutexLock mutexLock(queriesMutex);
        queries.swap(_queries);
    }
    for (Queries::iterator q = queries.begin();
         q != queries.end(); ++q) {
        LOG4CXX_DEBUG(_logger, "Deallocating query (" << q->second->getQueryID() << ")");
        try {
            q->second->handleAbort();
        } catch (Exception&) { }
    }
}

size_t Query::visitQueries(const Visitor& visit)
{
    ScopedMutexLock mutexLock(queriesMutex);

    if (visit)
    {
        BOOST_FOREACH(const Queries::value_type& i,_queries)
        {
            visit(i.second);
        }
    }

    return _queries.size();
}

void dumpMemoryUsage(const QueryID queryId)
{
#ifndef SCIDB_CLIENT
    if (Config::getInstance()->getOption<bool>(CONFIG_OUTPUT_PROC_STATS)) {
        const size_t* mstats = getMallocStats();
        LOG4CXX_DEBUG(Query::_logger,
                      "Stats after query ID ("<<queryId<<"): "
                      <<"Allocated size for PersistentChunks: " << StorageManager::getInstance().getUsedMemSize()
                      <<", allocated size for network messages: " << NetworkManager::getInstance()->getUsedMemSize()
                      <<", MAX size for MemChunks: "<< SharedMemCache::getInstance().getMemThreshold()
                      <<", allocated size for MemChunks: " << SharedMemCache::getInstance().getUsedMemSize()
                      <<", MemChunks were swapped out: " << SharedMemCache::getInstance().getSwapNum()
                      <<", MemChunks were loaded: " << SharedMemCache::getInstance().getLoadsNum()
                      <<", MemChunks were dropped: " << SharedMemCache::getInstance().getDropsNum()
                      <<", number of mallocs: " << (mstats ? mstats[0] : 0)
                      <<", number of frees: "   << (mstats ? mstats[1] : 0));
    }
#endif
}

void Query::destroy()
{
    std::shared_ptr<Array> resultArray;
    std::shared_ptr<RemoteMergedArray> mergedArray;
    std::shared_ptr<WorkQueue> bufferQueue;
    std::shared_ptr<WorkQueue> errQueue;
    std::shared_ptr<WorkQueue> opQueue;
    // XXX TODO: remove the context as wellto avoid potential memory leak
    std::shared_ptr<ReplicationContext> replicationCtx;
    {
        ScopedMutexLock cs(errorMutex);

        LOG4CXX_TRACE(_logger, "Cleaning up query (" << getQueryID() << ")");

        // Drop all unprocessed messages and cut any circular references
        // (from MessageHandleJob to Query).
        // This should be OK because we broadcast either
        // the error or abort before dropping the messages

        _bufferReceiveQueue.swap(bufferQueue);
        _errorQueue.swap(errQueue);
        _operatorQueue.swap(opQueue);
        _replicationCtx.swap(replicationCtx);

        // Unregister this query from liveness notifications
        InstanceLivenessNotification::removePublishListener(_livenessListenerID);

        // The result array may also have references to this query
        _currentResultArray.swap(resultArray);

        _mergedArray.swap(mergedArray);
    }
    if (bufferQueue) { bufferQueue->stop(); }
    if (errQueue)    { errQueue->stop(); }
    if (opQueue)     { opQueue->stop(); }
    dumpMemoryUsage(getQueryID());
}

void
BroadcastAbortErrorHandler::handleError(const std::shared_ptr<Query>& query)
{
    if (query->getQueryID() == 0) {
        return;
    }
    if (query->getQueryID() == INVALID_QUERY_ID) {
        assert(false);
        return;
    }
    if (! query->isCoordinator()) {
        assert(false);
        return;
    }
    LOG4CXX_DEBUG(_logger, "Broadcast ABORT message to all instances for query " << query->getQueryID());
    std::shared_ptr<MessageDesc> abortMessage = makeAbortMessage(query->getQueryID());
    // query may not have the instance map, so broadcast to all
    NetworkManager::getInstance()->broadcastPhysical(abortMessage);
}

void Query::freeQuery(QueryID queryID)
{
    ScopedMutexLock mutexLock(queriesMutex);
    Queries::iterator i = _queries.find(queryID);
    if (i != _queries.end()) {
        std::shared_ptr<Query> q = i->second;
        LOG4CXX_DEBUG(_logger, "Deallocating query (" << q->getQueryID() << ")");
        _queries.erase(i);
    }
}

bool Query::validate()
{
   bool isShutdown = NetworkManager::isShutdown();
   if (isShutdown) {
       handleAbort();
   }

   ScopedMutexLock cs(errorMutex);
   checkNoError();
   return true;
}

Query::OperatorContext::~OperatorContext()
{
}

void Query::setOperatorContext(std::shared_ptr<OperatorContext> const& opContext,
                               std::shared_ptr<JobQueue> const& jobQueue)
{
    assert(opContext);
    ScopedMutexLock lock(errorMutex);
    _operatorContext = opContext;
    _operatorQueue->start(jobQueue);
}

void Query::unsetOperatorContext()
{
    assert(_operatorContext);
    ScopedMutexLock lock(errorMutex);
    _operatorContext.reset();
    _operatorQueue->stop();
}

ostream& writeStatistics(ostream& os, std::shared_ptr<PhysicalQueryPlanNode> node, size_t tab)
{
    string tabStr(tab*4, ' ');
    std::shared_ptr<PhysicalOperator> op = node->getPhysicalOperator();
    os << tabStr << "*" << op->getPhysicalName() << "*: " << endl;
    writeStatistics(os, op->getStatistics(), tab + 1);
    for (size_t i = 0; i < node->getChildren().size(); i++) {
        writeStatistics(os, node->getChildren()[i], tab + 1);
    }
    return os;
}


std::ostream& Query::writeStatistics(std::ostream& os) const
{
    os << endl << "=== Query statistics: ===" << endl;
    scidb::writeStatistics(os, statistics, 0);
    for (size_t i = 0; (i < _physicalPlans.size()) && _physicalPlans[i]->getRoot(); i++)
    {
        os << "=== Statistics of plan #" << i << ": ===" << endl;
        scidb::writeStatistics(os, _physicalPlans[i]->getRoot(), 0);
    }
    os << endl << "=== Current state of system statistics: ===" << endl;
    scidb::writeStatistics(os, StatisticsScope::systemStatistics, 0);
    return os;
}

void Query::postWarning(const Warning& warn)
{
    ScopedMutexLock lock(_warningsMutex);
    _warnings.push_back(warn);
}

std::vector<Warning> Query::getWarnings()
{
    ScopedMutexLock lock(_warningsMutex);
    return _warnings;
}

void Query::clearWarnings()
{
    ScopedMutexLock lock(_warningsMutex);
    _warnings.clear();
}

void RemoveErrorHandler::handleError(const std::shared_ptr<Query>& query)
{
    boost::function<bool()> work = boost::bind(&RemoveErrorHandler::handleRemoveLock, _lock, true);
    Query::runRestartableWork<bool, Exception>(work);
}

bool RemoveErrorHandler::handleRemoveLock(const std::shared_ptr<SystemCatalog::LockDesc>& lock,
                                          bool forceLockCheck)
{
   assert(lock);
   assert(lock->getLockMode() == SystemCatalog::LockDesc::RM);

   std::shared_ptr<SystemCatalog::LockDesc> coordLock;
   if (!forceLockCheck) {
      coordLock = lock;
   } else {
      coordLock = SystemCatalog::getInstance()->checkForCoordinatorLock(lock->getArrayName(),
                                                                        lock->getQueryId());
   }
   if (!coordLock) {
      LOG4CXX_DEBUG(_logger, "RemoveErrorHandler::handleRemoveLock"
                    " lock does not exist. No abort action for query "
                    << lock->getQueryId());
      return false;
   }

   bool rc;
   if (coordLock->getArrayVersion() == 0)
   {
       LOG4CXX_DEBUG(_logger, "RemoveErrorHandler::handleRemoveLock" <<
                     " lock queryID="<< coordLock->getQueryId() <<
                     " lock array name="<< coordLock->getArrayName());
       rc = SystemCatalog::getInstance()->deleteArray(coordLock->getArrayName());
   }
   else
   {
       LOG4CXX_DEBUG(_logger, "RemoveErrorHandler::handleRemoveLock" <<
                     " lock queryID="<< coordLock->getQueryId() <<
                     " lock array name="<< coordLock->getArrayName() <<
                     " lock array version="<< coordLock->getArrayVersion());

       rc = SystemCatalog::getInstance()->deleteArrayVersions(coordLock->getArrayName(),
                                                              coordLock->getArrayVersion());
   }
   return rc;
}

void UpdateErrorHandler::handleError(const std::shared_ptr<Query>& query)
{
    boost::function<void()> work = boost::bind(&UpdateErrorHandler::_handleError, this, query);
    Query::runRestartableWork<void, Exception>(work);
}

void UpdateErrorHandler::_handleError(const std::shared_ptr<Query>& query)
{
   assert(query);
   if (!_lock) {
      assert(false);
      LOG4CXX_TRACE(_logger,
                    "Update error handler has nothing to do for query ("
                    << query->getQueryID() << ")");
      return;
   }
   assert(_lock->getInstanceId() == Cluster::getInstance()->getLocalInstanceId());
   assert( (_lock->getLockMode() == SystemCatalog::LockDesc::CRT)
           || (_lock->getLockMode() == SystemCatalog::LockDesc::WR) );
   assert(query->getQueryID() == _lock->getQueryId());

   LOG4CXX_DEBUG(_logger,
                 "Update error handler is invoked for query ("
                 << query->getQueryID() << ")");

   RollbackWork rw = bind(&UpdateErrorHandler::doRollback, _1, _2, _3);

   if (_lock->getInstanceRole() == SystemCatalog::LockDesc::COORD) {
      handleErrorOnCoordinator(_lock, rw);
   } else {
      assert(_lock->getInstanceRole() == SystemCatalog::LockDesc::WORKER);
      handleErrorOnWorker(_lock, query->isForceCancelled(), rw);
   }
   return;
}

void UpdateErrorHandler::releaseLock(const std::shared_ptr<SystemCatalog::LockDesc>& lock,
                                     const std::shared_ptr<Query>& query)
{
   assert(lock);
   assert(query);
   boost::function<bool()> work = boost::bind(&SystemCatalog::unlockArray,
                                             SystemCatalog::getInstance(),
                                             lock);
   bool rc = Query::runRestartableWork<bool, Exception>(work);
   if (!rc) {
      LOG4CXX_WARN(_logger, "Failed to release the lock for query ("
                   << query->getQueryID() << ")");
   }
}

static bool isTransientArray(const std::shared_ptr<SystemCatalog::LockDesc> & lock)
{
    return ( lock->getArrayId() > 0 &&
             lock->getArrayId() == lock->getArrayVersionId() &&
             lock->getArrayVersion() == 0 );
}


void UpdateErrorHandler::handleErrorOnCoordinator(const std::shared_ptr<SystemCatalog::LockDesc> & lock,
                                                  RollbackWork& rollback)
{
   assert(lock);
   assert(lock->getInstanceRole() == SystemCatalog::LockDesc::COORD);

   string const& arrayName = lock->getArrayName();

   std::shared_ptr<SystemCatalog::LockDesc> coordLock =
      SystemCatalog::getInstance()->checkForCoordinatorLock(arrayName,
                                                            lock->getQueryId());
   if (!coordLock) {
      LOG4CXX_DEBUG(_logger, "UpdateErrorHandler::handleErrorOnCoordinator:"
                    " coordinator lock does not exist. No abort action for query "
                    << lock->getQueryId());
      return;
   }

   if (isTransientArray(coordLock)) {
       SCIDB_ASSERT(false);
       // no rollback for transient arrays
       return;
   }

   const ArrayID unversionedArrayId  = coordLock->getArrayId();
   const VersionID newVersion      = coordLock->getArrayVersion();
   const ArrayID newArrayVersionId = coordLock->getArrayVersionId();

   if (unversionedArrayId == 0) {
       SCIDB_ASSERT(newVersion == 0);
       SCIDB_ASSERT(newArrayVersionId == 0);
       // the query has not done much progress, nothing to rollback
       return;
   }

   ASSERT_EXCEPTION(newVersion > 0,
                    string("UpdateErrorHandler::handleErrorOnCoordinator:")+
                    string(" inconsistent newVersion<=0"));
   ASSERT_EXCEPTION(unversionedArrayId > 0,
                    string("UpdateErrorHandler::handleErrorOnCoordinator:")+
                    string(" inconsistent unversionedArrayId<=0"));
   ASSERT_EXCEPTION(newArrayVersionId > 0,
                    string("UpdateErrorHandler::handleErrorOnCoordinator:")+
                    string(" inconsistent newArrayVersionId<=0"));

   const VersionID lastVersion = SystemCatalog::getInstance()->getLastVersion(unversionedArrayId);

   if (lastVersion == newVersion) {
       // we are done, the verson is committed
       return;
   }
   SCIDB_ASSERT(lastVersion < newVersion);
   SCIDB_ASSERT(lastVersion == (newVersion-1));

   if (rollback) {

       LOG4CXX_DEBUG(_logger, "UpdateErrorHandler::handleErrorOnCoordinator:"
                     " the new version "<< newVersion
                     <<" of array " << arrayName
                     <<" (arrId="<< newArrayVersionId <<")"
                     <<" is being rolled back for query ("
                     << lock->getQueryId() << ")");

       rollback(lastVersion, unversionedArrayId, newArrayVersionId);
   }
}

void UpdateErrorHandler::handleErrorOnWorker(const std::shared_ptr<SystemCatalog::LockDesc>& lock,
                                             bool forceCoordLockCheck,
                                             RollbackWork& rollback)
{
   assert(lock);
   assert(lock->getInstanceRole() == SystemCatalog::LockDesc::WORKER);

   string const& arrayName   = lock->getArrayName();
   VersionID newVersion      = lock->getArrayVersion();
   ArrayID newArrayVersionId = lock->getArrayVersionId();

   LOG4CXX_TRACE(_logger, "UpdateErrorHandler::handleErrorOnWorker:"
                 << " forceLockCheck = "<< forceCoordLockCheck
                 << " arrayName = "<< arrayName
                 << " newVersion = "<< newVersion
                 << " newArrayVersionId = "<< newArrayVersionId);

   if (newVersion != 0) {

       if (forceCoordLockCheck) {
           std::shared_ptr<SystemCatalog::LockDesc> coordLock;
           do {  //XXX TODO: fix the wait, possibly with batching the checks
               coordLock = SystemCatalog::getInstance()->checkForCoordinatorLock(arrayName,
                                                                                 lock->getQueryId());
               Query::waitForSystemCatalogLock();
           } while (coordLock);
       }
       ArrayID arrayId = lock->getArrayId();
       if(arrayId == 0) {
           LOG4CXX_WARN(_logger, "Invalid update lock for query ("
                        << lock->getQueryId()
                        << ") Lock:" << lock->toString()
                        << " No rollback is possible.");
       }
       VersionID lastVersion = SystemCatalog::getInstance()->getLastVersion(arrayId);

       LOG4CXX_TRACE(_logger, "UpdateErrorHandler::handleErrorOnWorker:"
                     << " lastVersion = "<< lastVersion);

       assert(lastVersion <= newVersion);

       // if we checked the coordinator lock, then lastVersion == newVersion implies
       // that the commit succeeded, and we should not rollback.
       // if we are not checking the coordinator lock, then something failed locally
       // and it should not be possible that the coordinator committed---we should
       // definitely rollback.
       assert(forceCoordLockCheck || lastVersion < newVersion);

       if (lastVersion < newVersion && newArrayVersionId > 0 && rollback) {
           rollback(lastVersion, arrayId, newArrayVersionId);
       }
   }
   LOG4CXX_TRACE(_logger, "UpdateErrorHandler::handleErrorOnWorker:"
                     << " exit");
}

void UpdateErrorHandler::doRollback(VersionID lastVersion,
                                    ArrayID baseArrayId,
                                    ArrayID newArrayId)
{

    LOG4CXX_TRACE(_logger, "UpdateErrorHandler::doRollback:"
                  << " lastVersion = "<< lastVersion
                  << " baseArrayId = "<< baseArrayId
                  << " newArrayId = "<< newArrayId);
   // if a query stopped before the coordinator recorded the new array version id
   // there is no rollback to do

   assert(newArrayId>0);
   assert(baseArrayId>0);

   std::map<ArrayID,VersionID> undoArray;
   undoArray[baseArrayId] = lastVersion;
   try {
       StorageManager::getInstance().rollback(undoArray);
       StorageManager::getInstance().removeVersionFromMemory(baseArrayId, newArrayId);
   } catch (const scidb::Exception& e) {
       LOG4CXX_ERROR(_logger, "UpdateErrorHandler::doRollback:"
                     << " lastVersion = "<< lastVersion
                     << " baseArrayId = "<< baseArrayId
                     << " newArrayId = "<< newArrayId
                     << ". Error: "<< e.what());
       throw; //XXX TODO: anything to do ???
   }
}

void Query::releaseLocks(const std::shared_ptr<Query>& q)
{
    assert(q);
    LOG4CXX_DEBUG(_logger, "Releasing locks for query " << q->getQueryID());

    boost::function<uint32_t()> work = boost::bind(&SystemCatalog::deleteArrayLocks,
                                                   SystemCatalog::getInstance(),
                                                   Cluster::getInstance()->getLocalInstanceId(),
                                                   q->getQueryID(),
                                                   SystemCatalog::LockDesc::INVALID_ROLE);
    runRestartableWork<uint32_t, Exception>(work);
}

void Query::acquireLocks()
{
    SystemCatalog::QueryLocks locks;
    {
        ScopedMutexLock cs(errorMutex);
        validate();
        Query::Finalizer f = bind(&Query::releaseLocks, _1);
        pushFinalizer(f);
        assert(_finalizers.size() > 1);
        locks = _requestedLocks;
    }
    acquireLocksInternal(locks);
}

void Query::retryAcquireLocks()
{
    SystemCatalog::QueryLocks locks;
    {
        ScopedMutexLock cs(errorMutex);
        // try to assert that the lock release finalizer is in place
        assert(_finalizers.size() > 1);
        validate();
        locks = _requestedLocks;
    }
    if (locks.empty())
    {
        assert(false);
        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_UNREACHABLE_CODE) << "Query::retryAcquireLocks";
    }
    acquireLocksInternal(locks);
}

void Query::acquireLocksInternal(SystemCatalog::QueryLocks& locks)
{
    LOG4CXX_TRACE(_logger, "Acquiring "<< locks.size()
                  << " array locks for query " << _queryID);

    bool foundDeadInstances = (_coordinatorLiveness->getNumDead() > 0);
    try {
        SystemCatalog::ErrorChecker errorChecker = bind(&Query::validate, this);
        BOOST_FOREACH(const std::shared_ptr<SystemCatalog::LockDesc>& lock, locks)
        {
            assert(lock);
            assert(lock->getQueryId() == _queryID);
            LOG4CXX_TRACE(_logger, "Acquiring lock: " << lock->toString());

            if (foundDeadInstances && (lock->getLockMode() > SystemCatalog::LockDesc::RD)) {
                throw SYSTEM_EXCEPTION(SCIDB_SE_QPROC, SCIDB_LE_NO_QUORUM);
            }

            bool rc = SystemCatalog::getInstance()->lockArray(lock, errorChecker);
            if (!rc) {
                assert(false);
                throw std::runtime_error((string("Failed to acquire SystemCatalog lock")+lock->toString()).c_str());
            }
        }
        validate();

        // get the array metadata catalog version, i.e. 'timestamp' the arrays in use by this query
        if (!locks.empty()) {
            SystemCatalog::getInstance()->getCurrentVersion(locks);
        }
    } catch (const scidb::SystemCatalog::LockBusyException& e) {
        throw;
    } catch (std::exception&) {
        releaseLocks(shared_from_this());
        throw;
    }
    if (_logger->isDebugEnabled()) {
        LOG4CXX_DEBUG(_logger, "Acquired "<< locks.size() << " array locks for query " << _queryID);
        BOOST_FOREACH(const std::shared_ptr<SystemCatalog::LockDesc>& lock, locks)
        {
            LOG4CXX_DEBUG(_logger, "Acquired lock: " << lock->toString());
        }
    }
}

ArrayID
Query::getCatalogVersion(const std::string& arrayName, bool allowMissing) const
{
    assert(isCoordinator());
    // XXX TODO: currently synchronization is not used because
    // XXX TODO: it is called strictly before or after the query array lock acquisition
    if (_requestedLocks.empty() ) {
        // we have not acquired the locks yet
        return SystemCatalog::ANY_VERSION;
    }
    const std::string* unversionedNamePtr(&arrayName);
    std::string unversionedName;
    if (!ArrayDesc::isNameUnversioned(arrayName) ) {
        unversionedName = ArrayDesc::makeUnversionedName(arrayName);
        unversionedNamePtr = &unversionedName;
    }
    std::shared_ptr<SystemCatalog::LockDesc> key(make_shared<SystemCatalog::LockDesc>((*unversionedNamePtr),
                                                                                 getQueryID(),
                                                                                 Cluster::getInstance()->getLocalInstanceId(),
                                                                                 SystemCatalog::LockDesc::INVALID_ROLE,
                                                                                 SystemCatalog::LockDesc::INVALID_MODE));
    SystemCatalog::QueryLocks::const_iterator iter = _requestedLocks.find(key);
    if (iter == _requestedLocks.end() && allowMissing) {
        return SystemCatalog::ANY_VERSION;
    }
    ASSERT_EXCEPTION(iter!=_requestedLocks.end(),
                     string("Query::getCatalogVersion: unlocked array: ")+arrayName);
    const std::shared_ptr<SystemCatalog::LockDesc>& lock = (*iter);
    assert(lock->isLocked());
    return lock->getArrayCatalogId();
}

uint64_t Query::getLockTimeoutNanoSec()
{
    static const uint64_t WAIT_LOCK_TIMEOUT_MSEC = 2000;
    const uint32_t msec = _rng()%WAIT_LOCK_TIMEOUT_MSEC + 1;
    const uint64_t nanosec = msec*1000000;
    return nanosec;
}

void Query::waitForSystemCatalogLock()
{
    Thread::nanoSleep(getLockTimeoutNanoSec());
}

const ProcGrid* Query::getProcGrid() const
{
    // locking to ensure a single allocation
    // XXX TODO: consider always calling Query::getProcGrid() in MpiManager::checkAndSetCtx
    //           that should guarantee an atomic creation of _procGrid
    ScopedMutexLock lock(const_cast<Mutex&>(errorMutex));
    // logically const, but we made _procGrid mutable to allow the caching
    // NOTE: Tigor may wish to push this down into the MPI context when
    //       that code is further along.  But for now, Query is a fine object
    //       on which to cache the generated procGrid
    if (!_procGrid) {
        _procGrid = new ProcGrid(getInstancesCount());
    }
    return _procGrid;
}

void Query::listLiveInstances(InstanceVisitor& func)
{
    assert(func);
    ScopedMutexLock lock(errorMutex);

    // we may relax this assert once we support update on a subset of instances
    assert(Cluster::getInstance()->getInstanceMembership()->getInstances().size() == getInstancesCount());

    for (vector<InstanceID>::const_iterator iter = _liveInstances.begin(); iter != _liveInstances.end(); ++iter) {
        std::shared_ptr<Query> thisQuery(shared_from_this());
        func(thisQuery, (*iter));
    }
}

ReplicationContext::ReplicationContext(const std::shared_ptr<Query>& query, size_t nInstances)
: _query(query)
#ifndef NDEBUG // for debugging
,_chunkReplicasReqs(nInstances)
#endif
{
    // ReplicatonManager singleton is initialized at startup time
    if (_replicationMngr == NULL) {
        _replicationMngr = ReplicationManager::getInstance();
    }
}

ReplicationContext::QueueInfoPtr ReplicationContext::getQueueInfo(QueueID id)
{   // mutex must be locked
    QueueInfoPtr& qInfo = _inboundQueues[id];
    if (!qInfo) {
        int size = Config::getInstance()->getOption<int>(CONFIG_REPLICATION_RECEIVE_QUEUE_SIZE);
        assert(size>0);
        size = (size<1) ? 4 : size+4; // allow some minimal extra space to tolerate mild overflows
        qInfo = std::make_shared<QueueInfo>(NetworkManager::getInstance()->createWorkQueue(1,static_cast<uint64_t>(size)));
        assert(!qInfo->getArray());
        assert(qInfo->getQueue());
        qInfo->getQueue()->stop();
    }
    assert(qInfo->getQueue());
    return qInfo;
}

void ReplicationContext::enableInboundQueue(ArrayID aId, const std::shared_ptr<Array>& array)
{
    assert(array);
    assert(aId > 0);
    ScopedMutexLock cs(_mutex);
    QueueID qId(aId);
    QueueInfoPtr qInfo = getQueueInfo(qId);
    assert(qInfo);
    std::shared_ptr<scidb::WorkQueue> wq = qInfo->getQueue();
    assert(wq);
    qInfo->setArray(array);
    wq->start();
}

std::shared_ptr<scidb::WorkQueue> ReplicationContext::getInboundQueue(ArrayID aId)
{
    assert(aId > 0);
    ScopedMutexLock cs(_mutex);
    QueueID qId(aId);
    QueueInfoPtr qInfo = getQueueInfo(qId);
    assert(qInfo);
    std::shared_ptr<scidb::WorkQueue> wq = qInfo->getQueue();
    assert(wq);
    return wq;
}

std::shared_ptr<scidb::Array> ReplicationContext::getPersistentArray(ArrayID aId)
{
    assert(aId > 0);
    ScopedMutexLock cs(_mutex);
    QueueID qId(aId);
    QueueInfoPtr qInfo = getQueueInfo(qId);
    assert(qInfo);
    std::shared_ptr<scidb::Array> array = qInfo->getArray();
    assert(array);
    assert(qInfo->getQueue());
    return array;
}

void ReplicationContext::removeInboundQueue(ArrayID aId)
{
    // tigor:
    // Currently, we dont remove the queue until the query is destroyed.
    // The reason for this was that we did not have a sync point, and
    // each instance was not waiting for the INCOMING replication to finish.
    // But we now have a sync point here, to coordinate the storage manager
    // fluhes.  So we may be able to implement queue removal in the future.

    std::shared_ptr<Query> query(Query::getValidQueryPtr(_query));
    syncBarrier(0, query);
    syncBarrier(1, query);

    return;
}

namespace {
void generateReplicationItems(std::shared_ptr<MessageDesc>& msg,
                              std::vector<std::shared_ptr<ReplicationManager::Item> >* replicaVec,
                              const std::shared_ptr<Query>& query,
                              InstanceID iId)
{
    if (iId == query->getInstanceID()) {
        return;
    }
    std::shared_ptr<ReplicationManager::Item> item(new ReplicationManager::Item(iId, msg, query));
    replicaVec->push_back(item);
}
}
void ReplicationContext::replicationSync(ArrayID arrId)
{
    assert(arrId > 0);
    if (Config::getInstance()->getOption<size_t>(CONFIG_REDUNDANCY) <= 0) {
        return;
    }

    std::shared_ptr<MessageDesc> msg = std::make_shared<MessageDesc>(mtChunkReplica);
    std::shared_ptr<scidb_msg::Chunk> chunkRecord = msg->getRecord<scidb_msg::Chunk> ();
    chunkRecord->set_array_id(arrId);
    // tell remote instances that we are done replicating
    chunkRecord->set_eof(true);

    assert(_replicationMngr);
    std::shared_ptr<Query> query(Query::getValidQueryPtr(_query));
    msg->setQueryID(query->getQueryID());

    vector<std::shared_ptr<ReplicationManager::Item> > replicasVec;
    Query::InstanceVisitor f =
        boost::bind(&generateReplicationItems, msg, &replicasVec, _1, _2);
    query->listLiveInstances(f);
    // we may relax this assert once we support update on a subset of instances
    assert(Cluster::getInstance()->getInstanceMembership()->getInstances().size() == query->getInstancesCount());

    assert(replicasVec.size() == (query->getInstancesCount()-1));
    for (size_t i=0; i<replicasVec.size(); ++i) {
        const std::shared_ptr<ReplicationManager::Item>& item = replicasVec[i];
        assert(_replicationMngr);
        _replicationMngr->send(item);
    }
    for (size_t i=0; i<replicasVec.size(); ++i) {
        const std::shared_ptr<ReplicationManager::Item>& item = replicasVec[i];
        assert(_replicationMngr);
        _replicationMngr->wait(item);
        assert(item->isDone());
        assert(item->validate(false));
    }

    QueueInfoPtr qInfo;
    {
        ScopedMutexLock cs(_mutex);
        QueueID qId(arrId);
        qInfo = getQueueInfo(qId);
        assert(qInfo);
        assert(qInfo->getArray());
        assert(qInfo->getQueue());
    }
    // wait for all to ack our eof
    Semaphore::ErrorChecker ec = bind(&Query::validate, query);
    qInfo->getSemaphore().enter(replicasVec.size(), ec);
}

void ReplicationContext::replicationAck(InstanceID sourceId, ArrayID arrId)
{
    assert(arrId > 0);
    QueueInfoPtr qInfo;
    {
        ScopedMutexLock cs(_mutex);
        QueueID qId(arrId);
        qInfo = getQueueInfo(qId);
        assert(qInfo);
        assert(qInfo->getArray());
        assert(qInfo->getQueue());
    }
    // sourceId acked our eof
    qInfo->getSemaphore().release();
}

/// cached pointer to the ReplicationManager singeton
ReplicationManager*  ReplicationContext::_replicationMngr;

void ReplicationContext::enqueueInbound(ArrayID arrId, std::shared_ptr<Job>& job)
{
    assert(job);
    assert(arrId>0);
    assert(job->getQuery());
    ScopedMutexLock cs(_mutex);

    std::shared_ptr<WorkQueue> queryQ(getInboundQueue(arrId));

    if (Query::_logger->isTraceEnabled()) {
        std::shared_ptr<Query> query(job->getQuery());
        LOG4CXX_TRACE(Query::_logger, "ReplicationContext::enqueueInbound"
                      <<" job="<<job.get()
                      <<", queue="<<queryQ.get()
                      <<", arrId="<<arrId
                      << ", queryID="<<query->getQueryID());
    }
    assert(_replicationMngr);
    try {
        WorkQueue::WorkItem item = _replicationMngr->getInboundReplicationItem(job);
        queryQ->enqueue(item);
    } catch (const WorkQueue::OverflowException& e) {
        LOG4CXX_ERROR(Query::_logger, "ReplicationContext::enqueueInbound"
                      << ": Overflow exception from the message queue (" << queryQ.get()
                      << "): "<<e.what());
        std::shared_ptr<Query> query(job->getQuery());
        assert(query);
        assert(false);
        query->handleError(e.copy());
        throw;
    }
}

} // namespace

