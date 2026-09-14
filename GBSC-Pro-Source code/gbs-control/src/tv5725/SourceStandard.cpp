#include "SourceStandard.h"

#include "SyncProcessor.h"

namespace Tv5725 {

namespace {

const uint16_t ProgressiveVsyncStart = 14;
const uint16_t ProgressiveVsyncStop = 11;

}  // namespace

SourceStandard::SourceStandard(uint8_t videoStandardInput)
    : standard_(videoStandardInput)
{
}

bool SourceStandard::isProgressive() const
{
    return standard_ == 3 || standard_ == 4 || standard_ == 8 || standard_ == 9;
}

void SourceStandard::apply() const
{
    if (!isProgressive())
        return;

    SyncProcessor::writeSdVsyncStart(ProgressiveVsyncStart);
    SyncProcessor::writeSdVsyncStop(ProgressiveVsyncStop);
}

}  // namespace Tv5725
