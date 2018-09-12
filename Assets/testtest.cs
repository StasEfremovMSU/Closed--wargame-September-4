using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.AI;

public class testtest : MonoBehaviour {
	public NavMeshAgent main;
	public GameObject newPos;
	public int r ;
	// Use this for initialization
	void Start () {
		main = GetComponent<NavMeshAgent>();
	}
	
	// Update is called once per frame
	void Update () {
		main.SetDestination(newPos.transform.position);
	}
}
