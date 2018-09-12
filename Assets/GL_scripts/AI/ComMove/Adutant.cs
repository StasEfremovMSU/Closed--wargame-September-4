using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.AI;

/// <summary>
/// Висит на объекте, переносящим команды
/// </summary>
public class Adutant : MonoBehaviour {

	NavMeshAgent main;

	/// <summary>
	/// Пункт отправления
	/// </summary>
	Vector3 pos1;
	/// <summary>
	///  Пункт прибытия
	/// </summary>
	Vector3 targetpos;

	/// Объект прибытия

	GameObject target;

	/// <summary>
	/// описание 
	/// </summary>

	bool firststep = true; 

	// Use this for initialization
	void Start () {
		main = gameObject.GetComponent<NavMeshAgent> ();
	}
	
	// Update is called once per frame
	void Update () {

		/// Алгоритм такой:
		/// Если расстояние до объекта мало, то движется к объекьу
		/// Если расстояние больше, то двигаться к первой точке, пока флаг активен
		/// Флаг перестает быть активным, если он объет близко подошел к флагу

		if ((gameObject.transform.position - target.transform.position).magnitude < 20.0f) {

		} else {
			
			if (firststep == true) {
				main.SetDestination (targetpos);
			} else {
				main.SetDestination (target.transform.position);
				if ((gameObject.transform.position - targetpos).magnitude < 20.0f)
				{
					firststep = false; 
				}


			}
		}
}
	}