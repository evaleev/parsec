/*
 * Copyright (c) 2013      The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 *
 * These symbols are in a file by themselves to provide nice linker
 * semantics.  Since linkers generally pull in symbols by object
 * files, keeping these symbols as the only symbols in this file
 * prevents utility programs such as "ompi_info" from having to import
 * entire components just to query their version and parameters.
 */

#include "parsec/parsec_config.h"
#include "parsec/runtime.h"

#include "parsec/mca/sched/sched.h"
#include "parsec/mca/sched/rnd/sched_rnd.h"
#include "parsec/papi_sde.h"

/*
 * Local function
 */
static int sched_rnd_component_query(mca_base_module_t **module, int *priority);
static int sched_rnd_component_register(void);

/*
 * Instantiate the public struct with all of our public information
 * and pointers to our public functions in it
 */
const parsec_sched_base_component_t parsec_sched_rnd_component = {

    /* First, the mca_component_t struct containing meta information
       about the component itself */

    {
        PARSEC_SCHED_BASE_VERSION_2_0_0,

        /* Component name and version */
        "rnd",
        PARSEC_VERSION_MAJOR,
        PARSEC_VERSION_MINOR,

        /* Component open and close functions */
        NULL, /*< No open: sched_rnd is always available, no need to check at runtime */
        NULL, /*< No close: open did not allocate any resource, no need to release them */
        sched_rnd_component_query, 
        /*< specific query to return the module and add it to the list of available modules */
        sched_rnd_component_register, /*< Register at least the SDE events */
	"", /*< no reserve */
    },
    {
        /* The component has no metada */
        MCA_BASE_METADATA_PARAM_NONE,
        "", /*< no reserve */
    }
};

mca_base_component_t *sched_rnd_static_component(void)
{
    return (mca_base_component_t *)&parsec_sched_rnd_component;
}

static int sched_rnd_component_query(mca_base_module_t **module, int *priority)
{
    /* module type should be: const mca_base_module_t ** */
    void *ptr = (void*)&parsec_sched_rnd_module;
    *priority = 1;
    *module = (mca_base_module_t *)ptr;
    return MCA_SUCCESS;
}

static int sched_rnd_component_register(void)
{
    #if defined(PARSEC_PAPI_SDE)
    papi_sde_describe_counter(parsec_papi_sde_handle, "PARSEC::SCHEDULER::PENDING_TASKS::SCHED=RND",
                              "the number of pending tasks for the RND scheduler");
    papi_sde_describe_counter(parsec_papi_sde_handle, "PARSEC::SCHEDULER::PENDING_TASKS::QUEUE=<VPID>::SCHED=RND",
                              "the number of pending tasks for the RND scheduler on virtual process <VPID>");
#endif
    return MCA_SUCCESS;
}

