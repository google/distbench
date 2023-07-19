# Distbench node attributes and service constraints support

In case we want to simulate physiscal characteristics for the nodes inside of distbench and specify constraints when placing the services, distbench supports node attributes as part of their description, and service constraints referring to where the services can or cannot be placed inside the description of a test.

## Node manager attributes

Before starting node_managers, we need to run the test_sequencer:
```bash
#(On 1 server node)
distbench -c opt test_sequencer --port=10000
```

To specify node attributes we need to run distbench on node_manager mode and add attributes through the command line with the format name=value. For instance, suppose we want to initialize a node with an attribute called rack with value 'A', we would run the following command:
```bash
#(On another server node)
distbench -c opt node_manager --test_sequencer=$TEST_SEQUENCER_HOSTPORT --port=9999 rack=A
```

We can add as many attributes as we want sepparated by spaces:
```bash
distbench -c opt node_manager --test_sequencer=localhost:10000 --port=9999 rack=A distance=far weight=12
```

We can also assign a node id before specifying any attributes by just passing as a command line argument "nodei" where "i" is the integer we want to assign to the node id.
```bash
distbench -c opt node_manager --test_sequencer=localhost:10000 --port=9999 node1 rack=A distance=far weight=12
```
**Note**: the node id assignation has to be specified before any attributes assignation

## Service constraints

The service constraints have to be specified inside the distributed system description proto message. It is a map between the server names, and the constraint list they have.

A constraint list is message that consists of a collection of constraint sets, and acts as a logical AND, that is, the constraint list is satisfied if all the constraint sets are.

A constraint set is a proto message conformed by a collection of constraints and acts as a logical OR, if one of the constraints is satisfied, then the whole constraint set is satisfied. 

Finally, each contraint is another proto message that has different fields, it has the attribute name field, which specifies the name of attribute we are refering to. It contains a list of string values and of int values, each list represents the possible values the attribute is going to be compared against to. Just one of the lists should be used. We also have an optional field called modulus, in case we want to use modular arithmetic for our comparison and finally we have an enum field called relation, which is the actual type of comparison we will be doing against our values. This field can be either the EQUAL, NOT_EQUAL, MODULO_EQUAL and MODULO_NOT_EQUAL.

**Note**: The list of different values in the constraints also acts as a logical OR, so a constraint is satisfied if one of the values satisfies the relation. 

```yaml
    service_constraints {
        key: "server/0"
        value {
            constraint_list {
                constraint_sets {
                    constraints {
                        attribute_name: "attribute1"
                        string_values: "value1"
                        relation: EQUAL
                    }
                    constraints {
                        attribute_name: "attribute2"
                        string_values: "value1"
                        string_values: "value2"
                        relation: NOT_EQUAL
                    }
                }
                constraint_sets {
                    constraints{
                        attribute_name: "attribute3"
                        int64_values: 10
                        int64_values: 9
                        relation: EQUAL
                    }
                    constraints{
                        attribute_name: "attribute4"
                        int64_values: 11
                        relation: MODULO_EQUAL
                        modulus: 2
                    }
                }
            }
        }
    }
```

Services will then be placed into nodes according to the constraints they have through a greedy algorithm which will search for every service with constraints the first node that satisfies them and place it there.