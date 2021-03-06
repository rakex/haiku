/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */


#include <package/hpkg/BufferDataOutput.h>

#include <string.h>


namespace BPackageKit {

namespace BHPKG {


// #pragma mark - BBufferDataOutput


BBufferDataOutput::BBufferDataOutput(void* buffer, size_t size)
	:
	fBuffer(buffer),
	fSize(size),
	fBytesWritten(0)
{
}


ssize_t
BBufferDataOutput::Write(const void* buffer, size_t size)
{
	if (size == 0)
		return 0;
	if (size > fSize - fBytesWritten)
		return B_BAD_VALUE;

	memcpy((uint8*)fBuffer + fBytesWritten, buffer, size);
	fBytesWritten += size;

	return size;
}


}	// namespace BHPKG

}	// namespace BPackageKit
