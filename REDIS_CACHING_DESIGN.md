# Redis Caching Layer Implementation Design Document

## Overview

This document outlines the design for implementing a Redis caching layer in the chat server to improve performance by reducing database queries. The implementation will focus on caching frequently accessed data with appropriate expiration and invalidation strategies.

## 1. Data to be Cached

### 1.1 User Information
- **What**: User objects with id, name, password, and state
- **Key Pattern**: `user:{userid}`
- **Reason**: Frequently accessed during login, messaging, and friend operations
- **Expiration**: 30 minutes (user data rarely changes)

### 1.2 Friend Lists
- **What**: Lists of user's friends
- **Key Pattern**: `friends:{userid}`
- **Reason**: Expensive join query to retrieve friend lists
- **Expiration**: 15 minutes (friends change infrequently)

### 1.3 Group Information
- **What**: Group details including members
- **Key Pattern**: `group:{groupid}`
- **Reason**: Complex multi-table query to get group and member information
- **Expiration**: 10 minutes (groups change moderately)

### 1.4 User Online Status
- **What**: Current online status of users
- **Key Pattern**: `user:status:{userid}`
- **Reason**: Frequently checked during messaging operations
- **Expiration**: 5 minutes (status changes frequently)

### 1.5 Offline Messages Count
- **What**: Count of offline messages per user
- **Key Pattern**: `offline:count:{userid}`
- **Reason**: Frequently checked to notify users of new messages
- **Expiration**: 2 minutes (message count changes frequently)

## 2. Cache Expiration and Invalidation Strategies

### 2.1 Time-Based Expiration
All cached data will have a TTL (Time To Live) to ensure data freshness:
- User data: 30 minutes
- Friend lists: 15 minutes
- Group information: 10 minutes
- User status: 5 minutes
- Offline message counts: 2 minutes

### 2.2 Event-Based Invalidation
Cache invalidation will occur when data is modified:

#### User Operations
- **User Registration/Login**: Cache user data
- **User Status Change**: Invalidate `user:{userid}` and `user:status:{userid}`

#### Friend Operations
- **Add Friend**: Invalidate both users' friend lists (`friends:{userid1}`, `friends:{userid2}`)
- **Remove Friend**: Invalidate both users' friend lists

#### Group Operations
- **Create Group**: Cache new group information
- **Add Group Member**: Invalidate group cache (`group:{groupid}`)
- **Remove Group Member**: Invalidate group cache
- **Delete Group**: Remove group cache

#### Message Operations
- **Send Message**: Update offline message count if recipient is offline
- **Read Messages**: Reset offline message count

### 2.3 Cache Warming Strategy
- Pre-cache frequently accessed data during server startup
- Cache warm user data based on recent activity logs

## 3. Integration with MySQL Database Operations

### 3.1 Read-Through Pattern
When data is requested:
1. Check Redis cache first
2. If cache miss, query MySQL database
3. Store result in Redis with appropriate TTL
4. Return data

### 3.2 Write-Through Pattern
When data is modified:
1. Update MySQL database
2. Update/Invalidate Redis cache
3. Handle errors appropriately (see fallback mechanisms)

### 3.3 Cache-Aside Pattern for Complex Queries
For complex operations that are difficult to cache:
1. Query MySQL directly
2. Cache individual components for future use

## 4. Fallback Mechanisms

### 4.1 Redis Connection Failure
When Redis is unavailable:
- Fall back to direct MySQL queries
- Log error and continue operation
- Retry Redis connection periodically
- Do not cache data until Redis is available again

### 4.2 Cache Miss Handling
- Gracefully handle cache misses
- Query MySQL and populate cache
- Implement circuit breaker for repeated cache failures

### 4.3 Cache Consistency
- Use atomic operations where possible
- Implement retry logic for cache updates
- Log inconsistencies for monitoring

## 5. Performance Improvements Expected

### 5.1 Reduced Database Load
- 60-80% reduction in user data queries
- 70-85% reduction in friend list queries
- 50-70% reduction in group information queries

### 5.2 Improved Response Times
- User data retrieval: ~5ms (Redis) vs ~50ms (MySQL)
- Friend list retrieval: ~10ms (Redis) vs ~100ms (MySQL)
- Group information retrieval: ~15ms (Redis) vs ~150ms (MySQL)

### 5.3 Scalability Benefits
- Better horizontal scaling capabilities
- Reduced database connection pool usage
- Improved handling of concurrent requests

## 6. Implementation Plan

### 6.1 New Components to Create

#### RedisCache Class
A new class to handle all Redis caching operations:
- Connection management
- Cache operations (get, set, delete)
- Error handling and fallbacks

#### CacheManager Class
A singleton class to manage caching logic:
- Coordinate between database and cache layers
- Implement caching strategies
- Handle cache invalidation

### 6.2 Modifications to Existing Components

#### UserModel Class
- Add Redis cache integration for user queries
- Update cache on user registration/status changes

#### FriendModel Class
- Add Redis cache integration for friend list queries
- Invalidate cache on friend operations

#### GroupModel Class
- Add Redis cache integration for group queries
- Invalidate cache on group operations

#### OfflineMsgModel Class
- Add Redis cache for offline message counts
- Update cache on message operations

#### ChatService Class
- Update business logic to use cache-aware data access
- Handle cache invalidation events

### 6.3 Redis Key Design
- Use consistent naming convention: `{entity}:{id}[:{subentity}]`
- Use Redis hashes for complex objects
- Use Redis sets for collections (friend lists, group members)
- Use Redis sorted sets for time-ordered data

## 7. Monitoring and Metrics

### 7.1 Cache Hit/Miss Ratios
- Track cache performance for each data type
- Set alerts for low hit ratios

### 7.2 Response Time Metrics
- Compare cached vs non-cached operation times
- Monitor Redis latency

### 7.3 Error Tracking
- Log Redis connection failures
- Track cache consistency issues

## 8. Security Considerations

### 8.1 Data Sensitivity
- User passwords will not be cached
- Only necessary user information will be stored in cache

### 8.2 Cache Security
- Implement proper Redis authentication
- Use secure connection to Redis if required
- Regular cache cleanup to prevent memory exhaustion

## 9. Testing Strategy

### 9.1 Unit Tests
- Test cache operations in isolation
- Verify cache invalidation logic
- Test fallback mechanisms

### 9.2 Integration Tests
- Test end-to-end caching behavior
- Verify data consistency between cache and database
- Test performance improvements

### 9.3 Load Testing
- Simulate high concurrent access
- Measure performance gains
- Test cache eviction under load

## 10. Deployment Considerations

### 10.1 Redis Configuration
- Configure appropriate memory limits
- Set up Redis persistence if needed
- Configure connection pooling

### 10.2 Gradual Rollout
- Enable caching for specific operations first
- Monitor performance and errors
- Gradually expand caching coverage

### 10.3 Monitoring Setup
- Set up alerts for cache-related issues
- Monitor Redis memory usage
- Track application performance metrics