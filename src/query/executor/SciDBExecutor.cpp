/*
**
* BEGIN_COPYRIGHT
*
* This file is part of SciDB.
* Copyright (C) 2008-2012 SciDB, Inc.
*
* SciDB is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation version 3 of the License.
*
* SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the GNU General Public License for the complete license terms.
*
* You should have received a copy of the GNU General Public License
* along with SciDB.  If not, see <http://www.gnu.org/licenses/>.
*
* END_COPYRIGHT
*/

/*
 * @file SciDBExecutor.cpp
 *
 * @author  roman.simakov@gmail.com
 *
 * @brief SciDB API internal implementation to coordinate query execution.
 *
 * This implementation is used by server side of remote protocol and
 * can be loaded directly to user process and transform it into scidb node.
 * Maybe useful for debugging and embedding scidb into users applications.
 */

#include "stdlib.h"
#include <string>
#include "boost/shared_ptr.hpp"
#include "boost/make_shared.hpp"
#include "boost/bind.hpp"
#include "log4cxx/logger.h"

#include "SciDBAPI.h"
#include "network/Connection.h"
#include "array/StreamArray.h"
#include "system/Exceptions.h"
#include "query/QueryProcessor.h"
#include "query/parser/Serialize.h"
#include "network/NetworkManager.h"
#include "network/MessageUtils.h"
#include "system/Cluster.h"
#include "util/RWLock.h"


using namespace std;
using namespace boost;

namespace scidb
{

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.executor"));

/**
 * Engine implementation of the SciDBAPI interface
 */
class SciDBExecutor: public scidb::SciDB
{
    public:
    virtual ~SciDBExecutor() {}
    void* connect(const std::string& connectionString, uint16_t port) const {
        // Not needed to implement in engine
        assert(false);

        // Shutting down warning
        return NULL;
    }

    void disconnect(void* connection = NULL) const {
        // Not needed to implement in engine
        assert(false);
    }

    void fillUsedPlugins(const ArrayDesc& desc, vector<string>& plugins) const
    {
        for (size_t i = 0; i < desc.getAttributes().size(); i++) {
            const string& libName = TypeLibrary::getTypeLibraries().getObjectLibrary(desc.getAttributes()[i].getType());
            if (libName != "scidb")
                plugins.push_back(libName);
        }
        for (size_t i = 0; i < desc.getDimensions().size(); i++) {
            const string& libName = TypeLibrary::getTypeLibraries().getObjectLibrary(desc.getDimensions()[i].getType());
            if (libName != "scidb")
                plugins.push_back(libName);
        }
    }

   void prepareQuery(const std::string& queryString, bool afl, QueryResult& queryResult, void* connection) const
    {
        // Executing query string
        assert(!Query::getQueryByID(queryResult.queryID, false, false)); //XXXXXXXX throw exception

        shared_ptr<QueryProcessor> queryProcessor = QueryProcessor::create();
        shared_ptr<Query> query = queryProcessor->createQuery(queryString,
                                                              queryResult.queryID);
        assert(queryResult.queryID == query->getQueryID());
        CurrentQueryScope queryScope(query->getQueryID());
        StatisticsScope sScope(&query->statistics);
        LOG4CXX_TRACE(logger, "Parsing query(" << query->getQueryID() << "): " << queryString << "");

        Query::Finalizer f = bind(&Query::releaseLocks, _1);
        query->pushFinalizer(f);

        // first pass to collect the array names in the query
        queryProcessor->parseLogical(query, afl);

        queryProcessor->inferArrayAccess(query);

        query->acquireLocks();

        // second pass under the array locks
        queryProcessor->parseLogical(query, afl);
        LOG4CXX_TRACE(logger, "Query is parsed");
        const ArrayDesc& desc = queryProcessor->inferTypes(query);
        fillUsedPlugins(desc, queryResult.plugins);
        LOG4CXX_TRACE(logger, "Types of query are inferred");

        std::ostringstream planString;
        query->logicalPlan->toString(planString);
        queryResult.explainLogical = planString.str();

        queryResult.selective = !query->logicalPlan->getRoot()->isDdl();
        queryResult.requiresExclusiveArrayAccess = query->doesExclusiveArrayAccess();

        LOG4CXX_DEBUG(logger, "The query is prepared")
    }

    void executeQuery(const std::string& queryString, bool afl, QueryResult& queryResult, void* connection) const
    {
        const clock_t startClock = clock();
        assert(queryResult.queryID>0);

        // Executing query string
        boost::shared_ptr<Query> query = Query::getQueryByID(queryResult.queryID);
        boost::shared_ptr<QueryProcessor> queryProcessor = QueryProcessor::create();

        assert(query->getQueryID() == queryResult.queryID);
        CurrentQueryScope queryScope(query->getQueryID());
        StatisticsScope sScope(&query->statistics);
        LOG4CXX_TRACE(logger, "Executing query(" << query->getQueryID() << "): " << queryString << "")

        if (!query->logicalPlan->getRoot()) {
            throw USER_EXCEPTION(SCIDB_SE_QPROC, SCIDB_LE_QUERY_WAS_EXECUTED);
        }
        std::ostringstream planString;
        query->logicalPlan->toString(planString);
        queryResult.explainLogical = planString.str();
        // Note: Optimization will be performed while execution
        boost::shared_ptr<Optimizer> optimizer =  Optimizer::create();

        try {
           query->start();
           query->validate();

           while (queryProcessor->optimize(optimizer, query))
            {
                LOG4CXX_DEBUG(logger, "Query is optimized");

                const bool isDdl = query->getCurrentPhysicalPlan()->isDdl();
                query->isDDL = isDdl;
                LOG4CXX_DEBUG(logger, "The physical plan is detected as " << (isDdl ? "DDL" : "DML") );
                if (logger->isDebugEnabled())
                {
                    std::ostringstream planString;
                    query->getCurrentPhysicalPlan()->toString(planString);
                    LOG4CXX_DEBUG(logger, "\n" + planString.str());
                }

                // Execution of single part of physical plan
                queryProcessor->preSingleExecute(query);
                NetworkManager* networkManager = NetworkManager::getInstance();
                size_t nodesCount = query->getNodesCount();
                if (!isDdl)
                {
                    std::ostringstream planString;
                    query->getCurrentPhysicalPlan()->toString(planString);
                    query->statistics.explainPhysical += planString.str() + ";";

                    // Serialize physical plan and sending it out
                    const string physicalPlan = serializePhysicalPlan(query->getCurrentPhysicalPlan());
                    LOG4CXX_DEBUG(logger, "Query is serialized: " << planString.str());
                    boost::shared_ptr<MessageDesc> preparePhysicalPlanMsg = boost::make_shared<MessageDesc>(mtPreparePhysicalPlan);
                    boost::shared_ptr<scidb_msg::PhysicalPlan> preparePhysicalPlanRecord =
                        preparePhysicalPlanMsg->getRecord<scidb_msg::PhysicalPlan>();
                    preparePhysicalPlanMsg->setQueryID(query->getQueryID());
                    preparePhysicalPlanRecord->set_physical_plan(physicalPlan);
                    boost::shared_ptr<const NodeLiveness> queryLiveness(query->getCoordinatorLiveness());
                    serializeQueryLiveness(queryLiveness, preparePhysicalPlanRecord);

                    uint32_t redundancy = Config::getInstance()->getOption<int>(CONFIG_REDUNDANCY);
                    shared_ptr<const NodeMembership> membership(Cluster::getInstance()->getNodeMembership());
                    assert(membership);
                    if ((membership->getViewId() != queryLiveness->getViewId()) ||
                        ((nodesCount + redundancy) < membership->getNodes().size())) {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_EXECUTION, SCIDB_LE_NO_QUORUM2);
                    }
                    networkManager->sendOutMessage(preparePhysicalPlanMsg);
                    LOG4CXX_DEBUG(logger, "Prepare physical plan was sent out");
                    LOG4CXX_DEBUG(logger, "Waiting confirmation about preparing physical plan in queryID from "
                                  << nodesCount - 1 << " nodes")

                    Semaphore::ErrorChecker ec = bind(&Query::validate, query);
                    query->results.enter(nodesCount-1, ec);
                    boost::shared_ptr<MessageDesc> executePhysicalPlanMsg = boost::make_shared<MessageDesc>(mtExecutePhysicalPlan);
                    executePhysicalPlanMsg->setQueryID(query->getQueryID());
                    networkManager->sendOutMessage(executePhysicalPlanMsg);
                    LOG4CXX_DEBUG(logger, "Execute physical plan was sent out");
                }

                try {
                    // Execution of local part of physical plan
                    queryProcessor->execute(query);
                }
                catch (const std::bad_alloc& e) {
                        throw SYSTEM_EXCEPTION(SCIDB_SE_NO_MEMORY, SCIDB_LE_MEMORY_ALLOCATION_ERROR) << e.what();
                }
                LOG4CXX_DEBUG(logger, "Query is executed locally");

                if (!isDdl)
                {
                    // Wait for results from every node except itself
                    Semaphore::ErrorChecker ec = bind(&Query::validate, query);
                    query->results.enter(nodesCount-1, ec);
                    LOG4CXX_DEBUG(logger, "The responses are received");
                    /**
                     * Check error state
                     */
                    query->validate();
                }

                queryProcessor->postSingleExecute(query);
            }
            query-> done();
        } catch (const Exception& e) {

            if (e.getShortErrorCode() != SCIDB_SE_THREAD)
            {
                shared_ptr<MessageDesc> errorMessage = makeErrorMessageFromException(e, query->getQueryID());
                NetworkManager::getInstance()->broadcast(errorMessage); //query may not have the node map, so broadcast to all
            }
            query->done(e.copy());
            e.raise();
        }
        const clock_t stopClock = clock();
        query->statistics.executionTime = (stopClock - startClock) * 1000 / CLOCKS_PER_SEC;

        queryResult.queryID = query->getQueryID();
        queryResult.executionTime = query->statistics.executionTime;
        queryResult.explainPhysical = query->statistics.explainPhysical;

        queryResult.selective = query->getCurrentResultArray();
        if (queryResult.selective) {
            queryResult.array = query->getCurrentResultArray();
        }

        LOG4CXX_DEBUG(logger, "The result of query is returned")
    }

    void cancelQuery(QueryID queryID, void* connection) const
    {
        LOG4CXX_TRACE(logger, "Cancelling query " << queryID)
        boost::shared_ptr<Query> query = Query::getQueryByID(queryID);

        StatisticsScope sScope(&query->statistics);

        query->handleCancel();
    }

} _sciDBExecutor;


SciDB& getSciDBExecutor()
{
    return _sciDBExecutor;
}


}
